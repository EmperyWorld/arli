// arli-test-lifecycle — Э2 acceptance on a mock LLM (no live model):
//   basic Done · kill -9 worker → failed(worker_lost) + respawn · FIFO across dialogs ·
//   Cancel → cancelled · HardStop · idle timeout · reconcile after daemon SIGKILL · graceful SIGTERM.
// Usage: arli-test-lifecycle <arli-daemon> <arli-agent> <arli-mock-llm>
#include "lifecycle_harness.hpp"

#include <filesystem>
#include <future>

namespace {

using namespace lt;
namespace fs = std::filesystem;

struct Env {
  std::string daemon_bin, agent_bin, mock_bin;
  std::string state_dir, workdir, endpoint, llm_url;
  Proc mock, daemon;

  bool start_daemon(Client& c) {
    if (!daemon.start(daemon_bin, {endpoint},
                      {{"ARLI_STATE_DIR", state_dir}, {"ARLI_AGENT_BIN", agent_bin}, {"ARLI_LISTEN", endpoint}})) {
      return false;
    }
    if (!c.wait_health(5000)) {
      std::fprintf(stderr, "start_daemon: no Health\n");
      return false;
    }
    c.token = arli::secrets::load(fs::path(state_dir) / "ui_token");
    if (c.token.empty()) {
      std::fprintf(stderr, "start_daemon: no ui_token in %s\n", state_dir.c_str());
      return false;
    }
    if (!c.wait_worker("idle", 6000)) {
      arli::v1::HealthReply h;
      c.health(&h);
      std::fprintf(stderr, "start_daemon: worker_state=%s pid=%d\n", h.worker_state().c_str(), h.worker_pid());
      return false;
    }
    return true;
  }
};

void t_basic(Env& env, Client& c) {
  std::fprintf(stderr, "[basic]\n");
  LT_CHECK(c.set_model(env.llm_url, "echo", env.workdir, 600, 10), "SetSettings mock echo");
  const auto d = c.create_dialog("basic");
  const auto r = c.send(d, "hi");
  LT_CHECK(r.status.ok(), "Send ok");
  LT_CHECK(r.done() && r.done_text().find("echo: hi") != std::string::npos, "Done echo: hi");
  LT_CHECK(c.last_task(d).status() == "done", "task status done");
}

void t_kill9(Env& env, Client& c) {
  std::fprintf(stderr, "[kill -9 worker]\n");
  c.set_model(env.llm_url, "slow:8000", env.workdir, 600, 10);
  const auto d = c.create_dialog("kill9");
  auto fut = std::async(std::launch::async, [&] { return c.send(d, "slow"); });
  sleep_ms(700);
  arli::v1::HealthReply h;
  c.health(&h);
  LT_CHECK(h.worker_state() == "busy" && h.worker_pid() > 0, "worker busy with pid");
  const int victim = h.worker_pid();
  const auto t0 = std::chrono::steady_clock::now();
  kill(victim, SIGKILL);
  const auto r = fut.get();
  const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
  LT_CHECK(dt.count() <= 2000, "UI got the end of stream ≤ 2 s after kill -9");
  LT_CHECK(r.error_contains("disconnected"), "Error(worker disconnected) in UI");
  const auto task = c.last_task(d);
  LT_CHECK(task.status() == "failed" && task.error() == "worker_lost", "task failed(worker_lost)");
  LT_CHECK(c.wait_worker("idle", 6000), "worker respawned");
  arli::v1::HealthReply h2;
  c.health(&h2);
  LT_CHECK(h2.worker_pid() != victim, "new worker pid");
  sleep_ms(300);
  LT_CHECK(!fs::exists("/proc/" + std::to_string(victim)), "no zombie left for the killed worker");
  c.set_model(env.llm_url, "echo", env.workdir, 600, 10);
  const auto again = c.send(d, "again");
  LT_CHECK(again.done() && again.done_text().find("echo: again") != std::string::npos,
           "next Send works without daemon restart");
}

void t_fifo(Env& env, Client& c) {
  std::fprintf(stderr, "[two dialogs FIFO]\n");
  c.set_model(env.llm_url, "slow:1500", env.workdir, 600, 10);
  const auto d1 = c.create_dialog("fifo-1");
  const auto d2 = c.create_dialog("fifo-2");
  auto f1 = std::async(std::launch::async, [&] { return c.send(d1, "first"); });
  sleep_ms(300);
  auto f2 = std::async(std::launch::async, [&] { return c.send(d2, "second"); });
  sleep_ms(300);
  arli::v1::HealthReply h;
  c.health(&h);
  LT_CHECK(h.queue_depth() == 1, "Health.queue_depth == 1 while first runs");
  const auto second_same = c.send(d2, "dup");
  LT_CHECK(second_same.status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
           "second Send into the same dialog → FAILED_PRECONDITION");
  const auto r1 = f1.get();
  const auto r2 = f2.get();
  LT_CHECK(r1.done() && r2.done(), "both dialogs finished");
  LT_CHECK(r2.has([](const auto& e) { return e.has_queued() && e.queued().position() == 1; }),
           "second dialog saw Queued{position=1}");
  const auto t1 = c.last_task(d1);
  const auto t2 = c.last_task(d2);
  LT_CHECK(t1.status() == "done" && t2.status() == "done", "both tasks done");
  LT_CHECK(t2.finished_at() >= t1.finished_at(), "second finished after first");
}

void t_cancel(Env& env, Client& c) {
  std::fprintf(stderr, "[cancel]\n");
  c.set_model(env.llm_url, "slow:30000", env.workdir, 600, 5);
  const auto d = c.create_dialog("cancel");
  auto fut = std::async(std::launch::async, [&] { return c.send(d, "long"); });
  sleep_ms(700);
  arli::v1::HealthReply h;
  c.health(&h);
  const int pid_before = h.worker_pid();
  LT_CHECK(c.cancel(d), "Cancel accepted");
  LT_CHECK(c.cancel(d), "second Cancel is idempotent (still ok)");
  const auto r = fut.get();
  LT_CHECK(r.error_contains("отменено"), "Error(отменено) delivered");
  LT_CHECK(r.done(), "Done after cancel");
  LT_CHECK(c.last_task(d).status() == "cancelled", "task status cancelled");
  LT_CHECK(c.wait_worker("idle", 3000), "worker idle again");
  c.health(&h);
  LT_CHECK(h.worker_pid() == pid_before, "same worker pid (cooperative cancel, no kill)");
  LT_CHECK(!c.cancel(d), "Cancel with no active task → ok=false");
}

void t_hard_stop(Env& env, Client& c) {
  std::fprintf(stderr, "[hard stop]\n");
  c.set_model(env.llm_url, "slow:30000", env.workdir, 600, 10);
  const auto d = c.create_dialog("hardstop");
  auto fut = std::async(std::launch::async, [&] { return c.send(d, "long"); });
  sleep_ms(700);
  arli::v1::HealthReply h;
  c.health(&h);
  const int victim = h.worker_pid();
  LT_CHECK(c.hard_stop(d), "HardStop accepted");
  const auto r = fut.get();
  LT_CHECK(r.error_contains("остановлено"), "Error(остановлено) delivered");
  LT_CHECK(c.last_task(d).status() == "cancelled" && c.last_task(d).error() == "hard_stop",
           "task cancelled(hard_stop)");
  LT_CHECK(c.wait_worker("idle", 8000), "worker respawned after hard stop");
  arli::v1::HealthReply h2;
  c.health(&h2);
  LT_CHECK(h2.worker_pid() != victim, "new worker pid after hard stop");
}

void t_idle_timeout(Env& env, Client& c) {
  std::fprintf(stderr, "[idle timeout]\n");
  c.set_model(env.llm_url, "slow:30000", env.workdir, 2, 10);
  const auto d = c.create_dialog("idle");
  const auto t0 = std::chrono::steady_clock::now();
  const auto r = c.send(d, "quiet");
  const auto dt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0);
  LT_CHECK(dt.count() <= 6, "idle timeout fired within ~idle_timeout_sec");
  LT_CHECK(r.error_contains("таймаут"), "Error(таймаут) delivered");
  LT_CHECK(c.last_task(d).error() == "idle_timeout", "task failed(idle_timeout)");
  LT_CHECK(c.wait_worker("idle", 8000), "worker respawned after timeout kill");
}

void t_reconcile(Env& env, Client& c) {
  std::fprintf(stderr, "[reconcile after daemon SIGKILL]\n");
  c.set_model(env.llm_url, "slow:30000", env.workdir, 600, 10);
  const auto d = c.create_dialog("reconcile");
  auto fut = std::async(std::launch::async, [&] { return c.send(d, "running"); });
  sleep_ms(700);
  arli::v1::HealthReply h;
  c.health(&h);
  const int worker = h.worker_pid();
  env.daemon.kill9();
  fut.get();
  if (worker > 0) {
    kill(worker, SIGKILL);  // orphaned worker: in production the systemd cgroup does this
  }
  sleep_ms(200);
  c.reconnect(env.endpoint);
  LT_CHECK(env.start_daemon(c), "daemon restarted");
  const auto task = c.last_task(d);
  LT_CHECK(task.status() == "failed" && task.error() == "reconcile_startup", "stale running → failed(reconcile_startup)");
}

void t_shutdown(Env& env, Client& c) {
  std::fprintf(stderr, "[graceful shutdown]\n");
  arli::v1::HealthReply h;
  c.health(&h);
  const int worker = h.worker_pid();
  env.daemon.term();
  const int code = env.daemon.wait(8000);
  LT_CHECK(code == 0, "daemon exited 0 on SIGTERM within 8 s");
  sleep_ms(500);
  LT_CHECK(!pid_alive(worker), "worker terminated with the daemon");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s <arli-daemon> <arli-agent> <arli-mock-llm>\n", argv[0]);
    return 2;
  }
  Env env;
  env.daemon_bin = argv[1];
  env.agent_bin = argv[2];
  env.mock_bin = argv[3];
  char tmpl[] = "/tmp/arli-lifecycle-XXXXXX";
  env.state_dir = mkdtemp(tmpl);
  env.workdir = env.state_dir + "/workspace";
  fs::create_directories(env.workdir);
  const int grpc_port = free_port();
  const int llm_port = free_port();
  env.endpoint = "127.0.0.1:" + std::to_string(grpc_port);
  env.llm_url = "http://127.0.0.1:" + std::to_string(llm_port) + "/v1";
  if (!env.mock.start(env.mock_bin, {std::to_string(llm_port)}, {})) {
    return 2;
  }

  Client c(env.endpoint);
  if (!env.start_daemon(c)) {
    std::fprintf(stderr, "daemon did not come up at %s\n", env.endpoint.c_str());
    env.daemon.term();
    if (env.daemon.wait(5000) < 0) {
      env.daemon.kill9();
    }
    env.mock.kill9();
    return 2;
  }

  t_basic(env, c);
  t_kill9(env, c);
  t_fifo(env, c);
  t_cancel(env, c);
  t_hard_stop(env, c);
  t_idle_timeout(env, c);
  t_reconcile(env, c);
  t_shutdown(env, c);

  env.daemon.kill9();
  env.mock.kill9();
  fs::remove_all(env.state_dir);
  std::fprintf(stderr, g_failed ? "LIFECYCLE: %d check(s) FAILED\n" : "LIFECYCLE: all checks passed\n", g_failed);
  return g_failed ? 1 : 0;
}
