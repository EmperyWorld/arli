#include "watchdog.hpp"

#include "hub.hpp"
#include "log.hpp"
#include "spawn_agent.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <thread>

namespace arli {
namespace {

constexpr int kBackoffMinMs = 500;
constexpr int kBackoffMaxMs = 5000;
// Cold starts compete with model/container and desktop I/O. Five seconds caused
// healthy workers to be killed during loaded CI and on busy user sessions.
constexpr int kHelloTimeoutMs = 15000;
constexpr int kTermWaitMs = 3000;
constexpr int kTickMs = 250;

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Watchdog::Watchdog(std::string listen_endpoint) : endpoint_(std::move(listen_endpoint)) {}

Watchdog::~Watchdog() {
  stop();
}

void Watchdog::start() {
  stop_.store(false);
  thread_ = std::thread([this] { loop(); });
}

void Watchdog::stop() {
  if (!thread_.joinable()) {
    return;
  }
  stop_.store(true);
  thread_.join();
  auto& hub = OrchestratorHub::instance();
  const auto generation = hub.session_generation();
  kill_agent(kTermWaitMs);
  hub.on_session_end(generation);
}

void Watchdog::loop() {
  while (!stop_.load()) {
    try {
      step();
    } catch (const std::exception& e) {
      log::error("watchdog", e.what());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
  }
}

void Watchdog::step() {
  auto& hub = OrchestratorHub::instance();
  const long long now = now_ms();
  const auto generation = hub.session_generation();

  // 1. Reap: the worker died on its own (crash, kill -9, exit).
  if (reap_agent()) {
    hub.on_session_end(generation);
    next_spawn_ms_ = now + backoff_ms_;
    backoff_ms_ = std::min(backoff_ms_ * 2, kBackoffMaxMs);
  }

  // 2. Kill requests (cancel grace expired, hard stop, timeouts).
  if (auto k = hub.take_kill_request()) {
    log::warn("watchdog", "killing worker: " + k->reason);
    kill_agent(kTermWaitMs);
    hub.on_session_end(generation);
    next_spawn_ms_ = now;  // restart immediately: the user is waiting
    backoff_ms_ = kBackoffMinMs;
  }

  // 3. Hello timeout: process is alive but never attached a Session.
  if (agent_alive() && !hello_seen_) {
    if (hub.worker_ready()) {
      hello_seen_ = true;
      backoff_ms_ = kBackoffMinMs;
    } else if (now - spawned_at_ms_ > kHelloTimeoutMs) {
      log::warn("watchdog", "no Hello → kill " + log::kv("pid", agent_pid()) + " " +
                                log::kv("timeout_ms", static_cast<long long>(kHelloTimeoutMs)));
      kill_agent(500);
      hub.on_session_end(generation);
      next_spawn_ms_ = now + backoff_ms_;
      backoff_ms_ = std::min(backoff_ms_ * 2, kBackoffMaxMs);
    }
  }

  // 4. Respawn (never during daemon shutdown).
  if (!agent_alive() && now >= next_spawn_ms_ && !hub.shutting_down()) {
    hello_seen_ = false;
    const int pid = spawn_agent_worker(endpoint_);
    if (pid <= 0) {
      hub.fail_queued("agent worker not available (binary missing or fork failed)");
      next_spawn_ms_ = now + kBackoffMaxMs;
    } else {
      hub.set_worker_starting(true);
      spawned_at_ms_ = now;
      next_spawn_ms_ = now + backoff_ms_;
    }
  }

  // 5. Timeouts / dispatch.
  hub.tick(std::chrono::steady_clock::now());
}

}  // namespace arli
