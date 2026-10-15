#include "jobs.hpp"
#include "job_retention.hpp"
#include "job_lease.hpp"
#include <thread>
#include "redact.hpp"
#include "db.hpp"
#include "job_identity.hpp"
#include "job_channel.hpp"
#include "tools/process_policy.hpp"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
namespace arli {
ToolResult start_background(Database& db, const std::filesystem::path& workdir, const std::string& command,
                            const ShellRunOptions& options) {
  if(!db.job_capacity_available(options.dialog_id,options.task_id))
    return {false,"JOB_QUOTA: максимум 16 фоновых процессов, 8 на диалог, 4 на задание; остановите ненужные процессы"};
  // Supervisor survives the worker; its command must die if the supervisor is killed.
  auto launch = prepare_shell(workdir, command + "\narli_status=$?\narli_last=$!\n"
      "if [ -n \"$arli_last\" ]; then wait \"$arli_last\"; arli_bg=$?; "
      "if [ \"$arli_status\" -eq 0 ]; then arli_status=$arli_bg; fi; fi\nwait\nexit $arli_status", options, false);
  const auto helper = std::filesystem::read_symlink("/proc/self/exe").parent_path() / "arli-job-supervisor";
  if (access(helper.c_str(), X_OK) != 0) return {false, "job supervisor unavailable"};
  std::filesystem::create_directories(state_root() / "jobs");
  JobDirectoryLease launch_lease(state_root()/"jobs",true);
  if(!launch_lease)return {false,"job directory lease unavailable"};
  const auto fingerprints = std::filesystem::absolute(state_root()/"redaction.fingerprints");
  redact::enable_persistence(fingerprints);
  std::string directory = (std::filesystem::weakly_canonical(state_root()) / "jobs" / "job-XXXXXX").string();
  if (!mkdtemp(directory.data())) return {false, "cannot create private job directory"};
  const std::string log = directory + "/output.log";
  struct PendingDirectory {std::string log;bool registered=false;~PendingDirectory(){if(!registered)for(int n=0;n<100;++n){if(remove_job_files(log))break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}}} pending{log};
  std::string config = nlohmann::json{{"cwd",launch.cwd},{"argv",launch.argv},{"env",launch.env},{"log",log},{"fingerprints",fingerprints.string()}}.dump();
  if (config.size() > 1024 * 1024) return {false, "background command too large"};
  int pair[2]; if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) return {false, "socketpair failed"};
  struct Socket { int fd; ~Socket() { close(fd); } } parent{pair[0]};
  const int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (nullfd < 0) { close(pair[1]); return {false, "stdin unavailable"}; }
  const std::string helper_path = helper.string();
  char* argv[] = {const_cast<char*>(helper_path.c_str()), nullptr};
  auto env = process_pointers(launch.env);
  const auto mid = fork();
  if (mid == 0) {
    close(pair[0]);
    if (fork() != 0) _exit(0);
    if (setsid() < 0) _exit(127);
    dup2(nullfd, 0); dup2(nullfd, 1); dup2(nullfd, 2);
    if (dup2(pair[1], 3) < 0) _exit(127);
    fcntl(3, F_SETFD, 0); close_range(4, ~0U, 0);
    const int pid = getpid(); if (write(3, &pid, sizeof(pid)) != sizeof(pid)) _exit(127);
    execve(helper_path.c_str(), argv, env.data()); _exit(127);
  }
  close(pair[1]); close(nullfd);
  if (mid < 0) return {false, "fork failed"};
  int status = 0; while (waitpid(mid, &status, 0) < 0 && errno == EINTR) {}
  JobChannel channel(parent.fd);
  int pid = 0; uint32_t size = static_cast<uint32_t>(config.size()); char ready = 0;
  if (!channel.transfer(&pid, sizeof(pid), false) || !channel.transfer(&size, sizeof(size), true) ||
      !channel.transfer(config.data(), config.size(), true) || !channel.transfer(&ready, 1, false) || ready != 'R')
    return {false, "job supervisor startup failed"};
  const auto identity = process_identity(pid);
  if (identity.empty()) return {false, "job identity unavailable"};
  const auto id = db.add_job(pid, command, launch.cwd, log, identity, options.dialog_id, options.task_id, options.turn_id);
  pending.registered=true;
  ready = 'G';
  if (!channel.transfer(&ready, 1, true)) { db.set_job_status(id, "lost"); return {false, "job launch interrupted"}; }
  return {true, "в фоне supervisor pid " + std::to_string(pid) + ", id " + std::to_string(id) + "\nлог " + log};
}
}
