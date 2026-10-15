#include "hub.hpp"
#include <algorithm>
namespace arli {
std::int64_t OrchestratorHub::queue_interaction_locked(v1::OrchToWorker msg) {
  std::int64_t id = 0;
  if (interaction_ack_supported_) {
    id = db_->create_command_receipt(busy_task_);
    msg.set_command_id(id);
    const auto task = db_->get_task(busy_task_);
    pending_interactions_.emplace(id, PendingInteraction{msg, busy_task_, busy_dialog_, task->turn_id, Clock::now(), 1});
  }
  outbound_.push_back(std::move(msg));
  return id;
}
void OrchestratorHub::abandon_interactions_locked(std::int64_t task) {
  for (auto it = pending_interactions_.begin(); it != pending_interactions_.end();) {
    if (it->second.task != task) { ++it; continue; }
    const auto id = it->first;
    db_->finish_command_receipt(id, "unconfirmed");
    std::erase_if(outbound_, [id](const auto& msg) { return msg.command_id() == id; });
    it = pending_interactions_.erase(it);
  }
}
bool OrchestratorHub::acknowledge_interaction(std::uint64_t generation, const v1::CommandAck& ack) {
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || generation != session_generation_ || !interaction_ack_supported_) return false;
    auto it = pending_interactions_.find(ack.command_id());
    if (it == pending_interactions_.end()) return true;
    const auto pending = it->second;
    db_->finish_command_receipt(ack.command_id(), ack.accepted() ? "accepted" : "rejected");
    pending_interactions_.erase(it);
    std::erase_if(outbound_, [&](const auto& msg) { return msg.command_id() == ack.command_id(); });
    v1::TurnEvent ev;
    ev.set_dialog_id(pending.dialog); ev.set_turn_id(pending.turn);
    if (ack.accepted()) {
      ev.mutable_status()->set_text(pending.message.has_ask_user_answer()
          ? "Ответ получен агентом" : "Решение по разрешению получено агентом");
      relay_locked(pending.task, ev, false, deferred);
    } else if (!cancel_sent_.contains(pending.task)) {
      ev.mutable_error()->set_message("Агент отклонил ответ или разрешение");
      finish_busy_locked(pending.task, "failed", "interaction_rejected", &ev, deferred);
      request_kill_locked("interaction rejected");
    }
  }
  run(deferred); cv_.notify_all(); return true;
}
void OrchestratorHub::retry_interactions_locked(Clock::time_point now, Deferred& out) {
  for (auto& [id, pending] : pending_interactions_) {
    if (cancel_sent_.contains(pending.task) || now - pending.sent < std::chrono::seconds(3)) continue;
    if (pending.attempts >= 3) {
      const auto ev = error_event(pending.dialog, pending.turn, "Агент не подтвердил получение ответа или разрешения");
      const auto task = pending.task;
      finish_busy_locked(task, "failed", "interaction_ack_timeout", &ev, out);
      request_kill_locked("interaction acknowledgement timeout");
      return; // finish_busy invalidates the map
    }
    if (std::none_of(outbound_.begin(), outbound_.end(), [id](const auto& msg) { return msg.command_id() == id; }))
      outbound_.push_back(pending.message);
    ++pending.attempts; pending.sent = now; cv_.notify_all();
  }
}
}  // namespace arli
