#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace arli {

struct PendingConfirm {
  std::int64_t dialog_id = 0;
  std::string token;
  std::string command;
};

// One pending danger confirm per process (single active turn in M1/M2).
class ConfirmHub {
 public:
  static ConfirmHub& instance();

  std::string begin(std::int64_t dialog_id, std::string command);
  // Blocks until accept/reject/cancel. Returns true if accepted.
  bool wait_decision(std::int64_t dialog_id, const std::string& token, std::atomic<bool>* cancel);
  bool decide(std::int64_t dialog_id, const std::string& token, bool accept);
  void reject_dialog(std::int64_t dialog_id);
  std::optional<PendingConfirm> pending() const;

 private:
  ConfirmHub() = default;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::optional<PendingConfirm> pending_;
  enum class Decision { None, Accept, Reject };
  Decision decision_ = Decision::None;
};

}  // namespace arli
