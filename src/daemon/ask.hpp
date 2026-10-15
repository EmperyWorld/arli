#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace arli {

// One pending ask_user per worker process (single active turn).
class AskHub {
 public:
  static AskHub& instance();

  void begin(std::int64_t dialog_id, std::string question, std::string question_id = {});
  // Blocks until answer/reject/cancel. Empty → rejected or cancelled.
  std::optional<std::string> wait_answer(std::int64_t dialog_id, std::atomic<bool>* cancel);
  bool answer(std::int64_t dialog_id, std::string text, const std::string& question_id = {});
  void reject_dialog(std::int64_t dialog_id);

 private:
  AskHub() = default;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::int64_t dialog_id_ = 0;
  std::string question_;
  std::string question_id_;
  bool pending_ = false;
  bool answered_ = false;
  std::string answer_;
};

}  // namespace arli
