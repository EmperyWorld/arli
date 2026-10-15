#include "confirm.hpp"

#include <chrono>
#include <random>
#include <sstream>

namespace arli {

ConfirmHub& ConfirmHub::instance() {
  static ConfirmHub hub;
  return hub;
}

std::string ConfirmHub::begin(std::int64_t dialog_id, std::string command) {
  std::lock_guard lock(mu_);
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::ostringstream tok;
  tok << std::hex << rng();
  pending_ = PendingConfirm{dialog_id, tok.str(), std::move(command)};
  decision_ = Decision::None;
  return pending_->token;
}

bool ConfirmHub::wait_decision(std::int64_t dialog_id, const std::string& token, std::atomic<bool>* cancel) {
  std::unique_lock lock(mu_);
  while (true) {
    if (cancel && cancel->load()) {
      pending_.reset();
      decision_ = Decision::None;
      return false;
    }
    if (decision_ != Decision::None) {
      const bool ok = decision_ == Decision::Accept;
      pending_.reset();
      decision_ = Decision::None;
      return ok;
    }
    cv_.wait_for(lock, std::chrono::milliseconds(200));
    if (!pending_ || pending_->dialog_id != dialog_id || pending_->token != token) {
      return false;
    }
  }
}

bool ConfirmHub::decide(std::int64_t dialog_id, const std::string& token, bool accept) {
  std::lock_guard lock(mu_);
  if (!pending_ || decision_ != Decision::None || pending_->dialog_id != dialog_id || pending_->token != token) {
    return false;
  }
  decision_ = accept ? Decision::Accept : Decision::Reject;
  cv_.notify_all();
  return true;
}

void ConfirmHub::reject_dialog(std::int64_t dialog_id) {
  std::lock_guard lock(mu_);
  if (pending_ && pending_->dialog_id == dialog_id) {
    decision_ = Decision::Reject;
    cv_.notify_all();
  }
}

std::optional<PendingConfirm> ConfirmHub::pending() const {
  std::lock_guard lock(mu_);
  return pending_;
}

}  // namespace arli
