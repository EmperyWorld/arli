#include "confirm.hpp"
#include "paths.hpp"
#include "tools.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

int main() {
  namespace fs = std::filesystem;
  const fs::path wd = arli::state_root() / "workspace";
  fs::create_directories(wd);

  const auto blocked = arli::dispatch_tool(wd, "run_shell", R"({"command":"echo hi"})");
  if (blocked.ok) {
    std::fprintf(stderr, "dispatch must not run shell\n");
    return 1;
  }
  std::printf("OK dispatch blocked\n");

  std::atomic<bool> cancel{false};
  std::atomic<bool> started{false};
  std::string token = arli::ConfirmHub::instance().begin(1, "echo smoke-confirm");
  started.store(true);

  std::thread waiter([&]() {
    const bool ok = arli::ConfirmHub::instance().wait_decision(1, token, &cancel);
    if (!ok) {
      std::fprintf(stderr, "wait rejected unexpectedly\n");
      return;
    }
    const auto ran = arli::tool_run_shell(wd, "echo smoke-confirm");
    if (!ran.ok || ran.output.find("smoke-confirm") == std::string::npos) {
      std::fprintf(stderr, "shell fail: %s\n", ran.output.c_str());
      return;
    }
    std::printf("OK shell after confirm\n");
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (arli::ConfirmHub::instance().decide(1, "bad-token", true)) {
    std::fprintf(stderr, "bad token accepted\n");
    cancel.store(true);
    waiter.join();
    return 4;
  }
  std::printf("OK bad token rejected\n");

  if (!arli::ConfirmHub::instance().decide(1, token, true)) {
    std::fprintf(stderr, "good token failed\n");
    cancel.store(true);
    waiter.join();
    return 6;
  }
  waiter.join();
  std::printf("CONFIRM_SMOKE_OK\n");
  return 0;
}
