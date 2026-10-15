#include "ask.hpp"

#include <chrono>

namespace arli {

AskHub& AskHub::instance() {
  static AskHub hub;
  return hub;
}

void AskHub::begin(std::int64_t dialog_id, std::string question, std::string question_id) {
  std::lock_guard lock(mu_);
  dialog_id_ = dialog_id;
  question_ = std::move(question);
  question_id_ = std::move(question_id);
  pending_ = true;
  answered_ = false;
  answer_.clear();
}

std::optional<std::string> AskHub::wait_answer(std::int64_t dialog_id, std::atomic<bool>* cancel) {
  std::unique_lock lock(mu_);
  while (pending_ && dialog_id_ == dialog_id) {
    if (cancel && cancel->load()) {
      pending_ = false;
      answered_ = false;
      answer_.clear();
      return std::nullopt;
    }
    if (answered_) {
      pending_ = false;
      answered_ = false;
      return answer_;
    }
    cv_.wait_for(lock, std::chrono::milliseconds(200));
  }
  return std::nullopt;
}

bool AskHub::answer(std::int64_t dialog_id, std::string text, const std::string& question_id) {
  std::lock_guard lock(mu_);
  if (!pending_ || answered_ || dialog_id_ != dialog_id || text.empty() ||
      (!question_id.empty() && question_id != question_id_)) {
    return false;
  }
  answer_ = std::move(text);
  answered_ = true;
  cv_.notify_all();
  return true;
}

void AskHub::reject_dialog(std::int64_t dialog_id) {
  std::lock_guard lock(mu_);
  if (pending_ && dialog_id_ == dialog_id) {
    pending_ = false;
    answered_ = false;
    answer_.clear();
    cv_.notify_all();
  }
}

}  // namespace arli
