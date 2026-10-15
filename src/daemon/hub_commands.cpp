#include "hub.hpp"
#include <algorithm>
namespace arli {
void OrchestratorHub::queue_claim_locked(v1::OrchToWorker msg) {
  if (claim_ack_supported_) {
    msg.set_command_id(db_->create_command_receipt(msg.claim().task_id()));
    pending_claim_ = PendingClaim{msg, Clock::now(), 1};
  }
  outbound_.push_back(std::move(msg));
}
void OrchestratorHub::abandon_claim_locked() {
  if (!pending_claim_) return;
  const auto id = pending_claim_->message.command_id();
  db_->finish_command_receipt(id, "unconfirmed");
  std::erase_if(outbound_, [id](const auto& msg) { return msg.command_id() == id; });
  pending_claim_.reset();
}
bool OrchestratorHub::acknowledge_claim(std::uint64_t generation, const v1::CommandAck& ack) {
  bool interaction = false;
  {
    std::lock_guard lock(mu_);
    interaction = pending_interactions_.contains(ack.command_id());
  }
  if (interaction) return acknowledge_interaction(generation, ack);
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || generation != session_generation_ || worker_id_.empty() ||
        (!claim_ack_supported_ && !interaction_ack_supported_) || ack.command_id() <= 0) return false;
    // Duplicate/late acknowledgements never affect another delivery.
    if (!pending_claim_ || pending_claim_->message.command_id() != ack.command_id()) return true;
    const auto claim = pending_claim_->message.claim();
    db_->finish_command_receipt(ack.command_id(), ack.accepted() ? "accepted" : "rejected");
    abandon_claim_locked();
    if (!ack.accepted()) {
      const auto ev = error_event(claim.dialog_id(), claim.turn_id(), "worker rejected claim");
      finish_busy_locked(claim.task_id(), "failed", "claim_rejected", &ev, deferred);
      request_kill_locked("claim rejected");
    }
  }
  run(deferred);
  return true;
}
void OrchestratorHub::retry_claim_locked(Clock::time_point now, Deferred& out) {
  if (!pending_claim_ || now - pending_claim_->sent < std::chrono::seconds(3)) return;
  if (pending_claim_->attempts >= 3) {
    const auto claim = pending_claim_->message.claim();
    const auto ev = error_event(claim.dialog_id(), claim.turn_id(), "worker did not acknowledge claim");
    finish_busy_locked(claim.task_id(), "failed", "claim_ack_timeout", &ev, out);
    request_kill_locked("claim acknowledgement timeout");
    return;
  }
  const auto id = pending_claim_->message.command_id();
  // Bound queue growth if the writer itself is blocked.
  if (std::none_of(outbound_.begin(), outbound_.end(), [id](const auto& msg) { return msg.command_id() == id; }))
    outbound_.push_back(pending_claim_->message);
  ++pending_claim_->attempts;
  pending_claim_->sent = now;
  cv_.notify_all();
}
}  // namespace arli
