#include "hub.hpp"
namespace arli {
bool OrchestratorHub::deliver_confirm(std::int64_t dialog_id, const std::string& token, bool accept, std::int64_t* command_id) {
  v1::OrchToWorker msg;
  msg.mutable_confirm_result()->set_dialog_id(dialog_id);
  msg.mutable_confirm_result()->set_confirm_token(token);
  msg.mutable_confirm_result()->set_accept(accept);
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || !busy_task_ || cancel_sent_.count(busy_task_) || busy_dialog_ != dialog_id ||
        token.empty() || token != pending_confirm_) return false;
    const auto id = queue_interaction_locked(std::move(msg));
    pending_confirm_.clear();
    if (command_id) *command_id = id;
  }
  cv_.notify_all();
  return true;
}

bool OrchestratorHub::deliver_ask_answer(std::int64_t dialog_id, const std::string& answer, const std::string& question_id, std::int64_t* command_id) {
  v1::OrchToWorker msg;
  msg.mutable_ask_user_answer()->set_dialog_id(dialog_id);
  msg.mutable_ask_user_answer()->set_answer(answer);
  msg.mutable_ask_user_answer()->set_question_id(question_id);
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || !busy_task_ || cancel_sent_.count(busy_task_) || busy_dialog_ != dialog_id || !pending_ask_) return false;
    if (answer.empty() || answer.size() > 16384 || (!question_id.empty() && question_id != pending_question_id_)) return false;
    msg.mutable_ask_user_answer()->set_question_id(pending_question_id_);
    const auto id = queue_interaction_locked(std::move(msg));
    pending_ask_ = false;
    if (command_id) *command_id = id;
  }
  cv_.notify_all();
  return true;
}

}
