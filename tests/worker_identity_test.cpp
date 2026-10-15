#include "spawn_agent.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <signal.h>
std::string child_token(int pid) {
  const auto path = "/proc/" + std::to_string(pid) + "/environ";
  for (int n = 0; n < 100; ++n) {
    std::ifstream input(path); std::string entry;
    while (std::getline(input, entry, '\0')) {
      const std::string prefix = "ARLI_WORKER_LAUNCH_TOKEN=";
      if (entry.rfind(prefix, 0) == 0) return entry.substr(prefix.size());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return {};
}
int main() {
  int failed = 0;
  auto check = [&](bool value) { if (!value) { ++failed; std::cerr << "worker identity assertion failed\n"; } };
  setenv("ARLI_AGENT_BIN", "/bin/sleep", 1);
  check(arli::worker_session_pid("") == 0);
  const int first = arli::spawn_agent_worker("30");
  const auto original = child_token(first);
  check(first > 0 && original.size() == 64 && arli::worker_session_pid(original) == first);
  check(arli::worker_session_pid("wrong") == 0 && arli::worker_session_pid("") == 0);
  check(arli::kill_agent(500));
  check(arli::worker_session_pid(original) == 0);
  const int second = arli::spawn_agent_worker("30");
  const auto rotated = child_token(second);
  check(second > 0 && rotated.size() == 64 && rotated != original);
  check(arli::worker_session_pid(original) == 0 && arli::worker_session_pid(rotated) == second);
  arli::signal_agent(SIGKILL);
  bool reaped = false;
  for (int n = 0; n < 100 && !reaped; ++n) {
    reaped = arli::reap_agent();
    if (!reaped) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  check(reaped && arli::worker_session_pid(rotated) == 0);
  arli::kill_agent(500);
  return failed ? 1 : 0;
}
