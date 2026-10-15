#include "jobs.hpp"
#include "db.hpp"
#include "job_identity.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <signal.h>
namespace fs = std::filesystem;
void pause_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
int main() {
  char root[] = "/tmp/arli-jobs-XXXXXX"; if (!mkdtemp(root)) return 2;
  setenv("ARLI_STATE_DIR", root, 1);
  const fs::path wd = fs::path(root) / "workspace"; fs::create_directories(wd);
  int failed = 0;
  auto check = [&](bool ok, const char* text) { if (!ok) { ++failed; std::cerr << text << '\n'; } };
  arli::Database db(std::string(root) + "/jobs.sqlite"); db.init();
  auto wait = [&](arli::JobRow job) {
    for (int n = 0; n < 100; ++n) { job = arli::refresh_job(db, job); if (job.status != "running") break; pause_ms(50); }
    return job;
  };
  auto result = arli::execute_shell(db, wd, "printf hello; exit 7", "", true);
  check(result.ok, "launch succeeds");
  if (!result.ok) { std::cerr << result.output << '\n'; return 1; }
  auto job = wait(db.list_jobs().front());
  check(job.status == "exited" && job.exit_code == 7, "actual nonzero exit status persisted");
  check(arli::job_log_tail(job.log_path) == "hello", "captured command output");
  check(!arli::stop_job_by_id(db, job.id), "finished job cannot be signalled");
  result = arli::execute_shell(db, wd, "sh -c 'exit 7' &", "", false);
  check(result.ok, "explicit asynchronous command launches"); job = wait(db.list_jobs().front());
  check(job.status == "exited" && job.exit_code == 7, "last asynchronous child failure is retained");
  result = arli::execute_shell(db, wd, "false", "", true);
  check(result.ok, "foreground expression in background job launches"); job = wait(db.list_jobs().front());
  check(job.exit_code == 1, "foreground status preserved without explicit exit");
  result = arli::execute_shell(db, wd, "python3 -c 'import sys; sys.stdout.write(\"x\"*4000000+\"END\")'", "", true);
  check(result.ok, "large output launch"); job = wait(db.list_jobs().front());
  check(job.status == "exited" && job.exit_code == 0, "large output completes");
  check(arli::job_log_tail(job.log_path).find("END") != std::string::npos, "final output is drained");
  check(fs::file_size(job.log_path) <= 1048576 && fs::exists(job.log_path + ".1") &&
        fs::file_size(job.log_path + ".1") <= 1048576, "logs bounded to two files");
  result = arli::execute_shell(db, wd, "while true; do echo tick >> heartbeat; sleep 0.05; done", "", true);
  check(result.ok, "long job launch"); job = db.list_jobs().front();
  for (int n = 0; n < 100 && !fs::exists(wd / "heartbeat"); ++n) pause_ms(20);
  check(fs::exists(wd / "heartbeat"), "job runs in sandbox");
  { arli::Database reopened(std::string(root) + "/jobs.sqlite"); reopened.init();
    const auto restored = reopened.get_job(job.id);
    check(restored && restored->process_identity == arli::process_identity(job.pid), "identity survives reopen");
    check(arli::stop_job_by_id(reopened, job.id), "pidfd stops restored supervisor"); }
  job = wait(job); check(job.status == "stopped" && job.exit_code >= 128, "stop records terminal status");
  if (fs::exists(wd / "heartbeat")) { const auto bytes = fs::file_size(wd / "heartbeat"); pause_ms(200);
    check(fs::file_size(wd / "heartbeat") == bytes, "descendants stopped too"); }
  result = arli::execute_shell(db, wd, "while true; do echo tick >> crash-heartbeat; sleep 0.05; done", "", true);
  check(result.ok, "crash test launch"); job = db.list_jobs().front();
  for (int n = 0; n < 100 && !fs::exists(wd / "crash-heartbeat"); ++n) pause_ms(20);
  kill(job.pid, SIGKILL); // Test owns this supervisor and intentionally simulates its crash.
  job = wait(job); check(job.status == "lost", "supervisor crash does not invent exit code");
  if (fs::exists(wd / "crash-heartbeat")) { pause_ms(200); const auto bytes = fs::file_size(wd / "crash-heartbeat");
    pause_ms(200); check(fs::file_size(wd / "crash-heartbeat") == bytes, "sandbox dies with crashed supervisor"); }
  const auto stale = db.add_job(getpid(), "stale", wd.string(), "/nonexistent", "foreign-boot:1");
  check(!arli::stop_job_by_id(db, stale) && db.get_job(stale)->status == "lost", "reused PID never signalled");
  const auto legacy = db.add_job(getpid(), "legacy", wd.string(), "/nonexistent");
  check(!arli::stop_job_by_id(db, legacy) && db.get_job(legacy)->status == "lost", "legacy rows fail closed");
  fs::remove_all(root); return failed ? 1 : 0;
}
