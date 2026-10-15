#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace arli {

// Supervises the single arli-agent process: reap, respawn with backoff, Hello timeout,
// executes kill requests from the hub, drives hub timeouts once a second.
class Watchdog {
 public:
  explicit Watchdog(std::string listen_endpoint);
  ~Watchdog();

  void start();
  // Stop the loop and terminate the worker (SIGTERM → wait → SIGKILL).
  void stop();

 private:
  void loop();
  void step();

  std::string endpoint_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  int backoff_ms_ = 500;
  long long next_spawn_ms_ = 0;
  long long spawned_at_ms_ = 0;
  bool hello_seen_ = false;
};

}  // namespace arli
