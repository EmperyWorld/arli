#include "spawn_agent.hpp"

#include "log.hpp"
#include "secrets.hpp"
#include <mutex>
#include <sys/random.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <spawn.h>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace arli {
namespace {

// Written by the watchdog thread, read by Health → atomic.
std::atomic<int> g_agent_pid{0};
std::string g_internal_token;
std::mutex g_launch_mu;
std::string g_launch_token;

// Minimal environment for the worker: no LLM keys, no shell/IDE leftovers from the parent.
constexpr const char* kEnvPassthrough[] = {"PATH", "HOME", "LANG", "LC_ALL", "TMPDIR",
                                           "XDG_DATA_HOME", "XDG_RUNTIME_DIR", "ARLI_LOG"
#ifdef ARLI_SANITIZER_BUILD
                                           , "ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"
#endif
};

std::vector<std::string> worker_environment(const std::string& listen_endpoint, const std::string& launch_token) {
  std::vector<std::string> env;
  for (const char* key : kEnvPassthrough) {
    if (const char* v = std::getenv(key); v && *v) {
      env.emplace_back(std::string(key) + "=" + v);
    }
  }
  env.emplace_back("ARLI_LISTEN=" + listen_endpoint);
  env.emplace_back("ARLI_WORKER_LAUNCH_TOKEN=" + launch_token);
  env.emplace_back("ARLI_WORKER_APPLY_RLIMITS=1");
  if (const char* st = std::getenv("ARLI_STATE_DIR"); st && *st) {
    env.emplace_back(std::string("ARLI_STATE_DIR=") + st);
  }
  if (!g_internal_token.empty()) {
    env.emplace_back("ARLI_INTERNAL_TOKEN=" + g_internal_token);
  }
  return env;
}

std::string self_dir() {
  std::error_code ec;
  auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) {
    return {};
  }
  return exe.parent_path().string();
}

bool pid_alive(int pid) {
  if (pid <= 0) {
    return false;
  }
  return kill(pid, 0) == 0;
}

}  // namespace

void set_worker_internal_token(std::string token) {
  g_internal_token = std::move(token);
}

std::string agent_binary_path() {
  if (const char* env = std::getenv("ARLI_AGENT_BIN"); env && *env) {
    return env;
  }
  const std::string dir = self_dir();
  if (!dir.empty()) {
    return dir + "/arli-agent";
  }
  return "arli-agent";
}

int worker_session_pid(std::string_view token) {
  std::lock_guard lock(g_launch_mu);
  return !g_launch_token.empty() && secrets::constant_time_equal(token, g_launch_token) ? g_agent_pid.load() : 0;
}

int agent_pid() {
  return g_agent_pid;
}

bool agent_alive() {
  return pid_alive(g_agent_pid);
}

bool reap_agent() {
  if (g_agent_pid <= 0) {
    return false;
  }
  const int pid = g_agent_pid.load();
  int st = 0;
  if (waitpid(pid, &st, WNOHANG) != pid) {
    return false;
  }
  if (WIFEXITED(st)) {
    log::info("orch", "arli-agent exited " + log::kv("pid", pid) + " " + log::kv("code", WEXITSTATUS(st)));
  } else if (WIFSIGNALED(st)) {
    log::warn("orch", "arli-agent killed " + log::kv("pid", pid) + " " + log::kv("signal", WTERMSIG(st)));
  }
  { std::lock_guard lock(g_launch_mu); g_agent_pid = 0; g_launch_token.clear(); }
  return true;
}

void signal_agent(int sig) {
  if (g_agent_pid > 0) {
    kill(g_agent_pid, sig);
  }
}

bool kill_agent(int term_wait_ms) {
  if (g_agent_pid <= 0) {
    return false;
  }
  const int pid = g_agent_pid;
  kill(pid, SIGTERM);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(term_wait_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (reap_agent()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  log::warn("orch", "arli-agent ignored SIGTERM → SIGKILL " + log::kv("pid", pid));
  kill(pid, SIGKILL);
  int st = 0;
  waitpid(pid, &st, 0);
  { std::lock_guard lock(g_launch_mu); g_agent_pid = 0; g_launch_token.clear(); }
  return true;
}

int spawn_agent_worker(const std::string& listen_endpoint) {
  if (pid_alive(g_agent_pid)) {
    return g_agent_pid;
  }
  reap_agent();
  g_agent_pid = 0;

  const std::string bin = agent_binary_path();
  if (!std::filesystem::exists(bin)) {
    log::error("orch", "arli-agent not found " + log::kv("bin", bin));
    return 0;
  }

  // Build the complete argv/envp before spawning the worker.
  unsigned char bytes[32];
  size_t filled = 0;
  while (filled < sizeof(bytes)) {
    const auto n = getrandom(bytes + filled, sizeof(bytes) - filled, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { log::error("orch", "worker launch entropy unavailable"); return 0; }
    filled += static_cast<size_t>(n);
  }
  std::string launch_token;
  for (const auto byte : bytes) { launch_token += "0123456789abcdef"[byte >> 4]; launch_token += "0123456789abcdef"[byte & 15]; }
  const std::vector<std::string> env_store = worker_environment(listen_endpoint, launch_token);
  std::vector<char*> envp;
  envp.reserve(env_store.size() + 1);
  for (const auto& e : env_store) {
    envp.push_back(const_cast<char*>(e.c_str()));
  }
  envp.push_back(nullptr);
  char* const argv[] = {const_cast<char*>("arli-agent"), const_cast<char*>(listen_endpoint.c_str()), nullptr};

  // Authentication waits for parent registration. posix_spawn avoids running any
  // daemon code between fork and exec in this multi-threaded process.
  std::lock_guard launch_lock(g_launch_mu);
  posix_spawnattr_t attr;
  int spawn_error = posix_spawnattr_init(&attr);
  if (spawn_error != 0) {
    log::error("orch", std::string("posix_spawnattr_init failed: ") + std::strerror(spawn_error));
    return 0;
  }

  sigset_t none;
  sigemptyset(&none);
  sigset_t defaults;
  sigemptyset(&defaults);
  sigaddset(&defaults, SIGINT);
  sigaddset(&defaults, SIGTERM);
  sigaddset(&defaults, SIGPIPE);
  spawn_error = posix_spawnattr_setsigmask(&attr, &none);
  if (spawn_error == 0) spawn_error = posix_spawnattr_setsigdefault(&attr, &defaults);
  if (spawn_error == 0) {
    spawn_error = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
  }

  pid_t pid = 0;
  if (spawn_error == 0) {
    spawn_error = posix_spawn(&pid, bin.c_str(), nullptr, &attr, argv, envp.data());
  }
  posix_spawnattr_destroy(&attr);
  if (spawn_error != 0) {
    log::error("orch", std::string("posix_spawn failed: ") + std::strerror(spawn_error));
    return 0;
  }
  g_launch_token = std::move(launch_token);
  g_agent_pid = static_cast<int>(pid);
  log::info("orch", "spawned arli-agent " + log::kv("pid", static_cast<long long>(pid)) + " " + log::kv("bin", bin));
  return static_cast<int>(pid);
}

}  // namespace arli
