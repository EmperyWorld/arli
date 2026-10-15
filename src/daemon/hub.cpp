// OrchestratorHub — state, Session, dispatch. Waiters: hub_waiters.cpp; control: hub_control.cpp.
#include "hub.hpp"
#include "task_settings.hpp"

#include "audit.hpp"
#include "log.hpp"

namespace arli {

OrchestratorHub& OrchestratorHub::instance() {
  static OrchestratorHub hub;
  return hub;
}

void OrchestratorHub::init(std::shared_ptr<Database> db) {
  std::lock_guard lock(mu_);
  db_ = std::move(db);
}

void OrchestratorHub::run(Deferred& d) {
  for (auto& f : d) {
    f();
  }
  d.clear();
}

v1::TurnEvent OrchestratorHub::error_event(std::int64_t dialog_id, std::int64_t turn_id, const std::string& msg) {
  v1::TurnEvent ev;
  ev.set_dialog_id(dialog_id);
  ev.set_turn_id(turn_id);
  ev.mutable_error()->set_message(msg);
  return ev;
}

// ----- worker state -----

bool OrchestratorHub::worker_ready() const {
  std::lock_guard lock(mu_);
  return session_live_ && !worker_id_.empty();
}

std::string OrchestratorHub::worker_id() const {
  std::lock_guard lock(mu_);
  return worker_id_;
}

std::string OrchestratorHub::worker_state() const {
  std::lock_guard lock(mu_);
  if (worker_doomed_) {
    return "stopping";
  }
  if (session_live_ && !worker_id_.empty()) {
    return busy_task_ ? "busy" : "idle";
  }
  return worker_starting_ ? "starting" : "none";
}

std::int64_t OrchestratorHub::busy_task() const {
  std::lock_guard lock(mu_);
  return busy_task_;
}

void OrchestratorHub::set_worker_starting(bool starting) {
  std::lock_guard lock(mu_);
  worker_starting_ = starting;
}

int OrchestratorHub::queue_depth() const {
  std::lock_guard lock(mu_);
  return db_ ? db_->count_queued() : 0;
}

// ----- Session -----

std::uint64_t OrchestratorHub::try_attach_session() {
  std::lock_guard lock(mu_);
  if (session_live_) {
    return false;
  }
  ++session_generation_;
  if (!session_generation_) ++session_generation_;
  session_live_ = true;
  worker_id_.clear();
  claim_ack_supported_ = false;
  interaction_ack_supported_ = false;
  dialog_instructions_supported_ = false;
  mutation_revert_supported_ = false;mutation_catalog_supported_=false;workspace_identity_supported_=false;
  outbound_.clear();
  cv_.notify_all();
  return session_generation_;
}

bool OrchestratorHub::session_active(std::uint64_t generation) const {
  std::lock_guard lock(mu_);
  return session_live_ && generation == session_generation_;
}

std::uint64_t OrchestratorHub::session_generation() const {
  std::lock_guard lock(mu_);
  return session_live_ ? session_generation_ : 0;
}

void OrchestratorHub::on_session_end(std::uint64_t generation) {
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || generation != session_generation_) {
      return;
    }
    abandon_interactions_locked(busy_task_);
    abandon_claim_locked();
    session_live_ = false;
    worker_starting_ = false;
    worker_doomed_ = false;
    worker_id_.clear();
    outbound_.clear();
    cancel_sent_.clear();
    cancel_deadline_.reset();
    action_started_.clear();
    if (busy_task_ != 0) {
      const std::int64_t lost = busy_task_;
      auto t = db_ ? db_->get_task(lost) : std::nullopt;
      const auto ev = error_event(busy_dialog_, t ? t->turn_id : 0, "agent worker disconnected");
      log::warn("orch", "worker lost: running task → failed " + log::kv("task", static_cast<long long>(lost)));
      finish_busy_locked(lost, "failed", "worker_lost", &ev, deferred);
    }
    // Any other running rows (should not exist) → failed as well; queued survive for the next worker.
    if (db_) {
      db_->fail_running_tasks("worker_lost");
    }
    busy_task_ = 0;
    pending_confirm_.clear();
    pending_ask_ = false;
    busy_dialog_ = 0;
    cv_.notify_all();
  }
  run(deferred);
}

std::optional<v1::OrchToWorker> OrchestratorHub::pop_outbound(std::uint64_t generation, int wait_ms) {
  std::unique_lock lock(mu_);
  cv_.wait_for(lock, std::chrono::milliseconds(wait_ms), [&] { return !outbound_.empty() || !session_live_ || generation != session_generation_; });
  if (!session_live_ || generation != session_generation_ || outbound_.empty()) {
    return std::nullopt;
  }
  auto msg = std::move(outbound_.front());
  outbound_.pop_front();
  return msg;
}

void OrchestratorHub::wake_outbound() {
  cv_.notify_all();
}

// ----- dispatch -----

bool OrchestratorHub::dispatch_locked(Deferred& out) {
  if (!db_ || shutting_down_ || worker_doomed_ || !session_live_ || worker_id_.empty() || busy_task_ != 0) {
    return false;
  }
  auto task = db_->next_queued();
  if (!task) {
    return false;
  }
  if (!db_->mark_running(task->id, worker_id_)) {
    return false;
  }
  const auto current = db_->get_settings();
  SettingsRow s = current;
  std::string dialog_instructions, revert_changeset, workspace_identity;int64_t revert_source_task=0,revert_source_turn=0;
  if (!task->config_json.empty()) {
    try {
      const auto snapshot = nlohmann::json::parse(task->config_json);
      s = snapshot.get<SettingsRow>();
      if(snapshot.contains("workspace_identity"))workspace_identity=snapshot.at("workspace_identity").dump();
      revert_source_task=snapshot.value("revert_source_task",int64_t{0});revert_source_turn=snapshot.value("revert_source_turn",int64_t{0});
      revert_changeset = snapshot.value("revert_changeset_id", "");
      dialog_instructions = snapshot.value("dialog_instructions", "");
    }
    catch (...) {
      const auto ev = error_event(task->dialog_id, task->turn_id, "invalid task settings snapshot");
      finish_busy_locked(task->id, "failed", "invalid_snapshot", &ev, out);
      return false;
    }
    if (revert_changeset.empty() && s.llm_base_url != current.llm_base_url) {
      const auto ev = error_event(task->dialog_id, task->turn_id, "model endpoint changed while queued; resend task");
      finish_busy_locked(task->id, "failed", "endpoint_changed", &ev, out);
      return false;
    }
    s.llm_api_key = current.llm_api_key;
  }
  if (!dialog_instructions.empty() && !dialog_instructions_supported_) {
    const auto ev = error_event(task->dialog_id, task->turn_id, "worker does not support pinned dialog instructions");
    finish_busy_locked(task->id, "failed", "worker_capability_missing", &ev, out);
    return false;
  }
  if (!revert_changeset.empty() && (!mutation_revert_supported_ || (revert_source_task && !mutation_catalog_supported_))) {
    const auto ev = error_event(task->dialog_id, task->turn_id, "worker does not support changeset revert");
    finish_busy_locked(task->id, "failed", "worker_capability_missing", &ev, out);
    return false;
  }
  if(!workspace_identity.empty()&&!workspace_identity_supported_){
    const auto ev=error_event(task->dialog_id,task->turn_id,"worker does not support workspace identity");
    finish_busy_locked(task->id,"failed","worker_capability_missing",&ev,out);return false;
  }
  turn_timeout_sec_ = s.turn_timeout_sec;
  idle_timeout_sec_ = s.idle_timeout_sec;
  cancel_grace_sec_ = s.cancel_grace_sec;

  v1::OrchToWorker msg;
  auto* claim = msg.mutable_claim();
  claim->set_task_id(task->id);
  claim->set_revert_changeset_id(revert_changeset);
  claim->set_revert_source_task_id(revert_source_task);claim->set_revert_source_turn_id(revert_source_turn);
  claim->set_dialog_id(task->dialog_id);
  claim->set_turn_id(task->turn_id);
  claim->set_user_text(task->text);
  claim->set_dialog_instructions(dialog_instructions);
  claim->set_workdir(s.workdir);claim->set_workspace_identity_json(workspace_identity);
  claim->set_llm_base_url(s.llm_base_url);
  claim->set_llm_model(s.llm_model);
  claim->set_llm_api_key(s.llm_api_key);
  claim->set_llm_profile(s.llm_profile);
  claim->set_llm_temperature(s.llm_temperature);
  claim->set_shell_auto(s.shell_auto);
  claim->set_shell_ask(s.shell_ask);
  claim->set_shell_allow_all(s.shell_allow_all);
  claim->set_delete_confirm(s.delete_confirm);
  claim->set_list_ignore(s.list_ignore);
  claim->set_context_tokens(s.context_tokens);
  claim->set_max_steps(s.max_steps);
  claim->set_shell_timeout_sec(s.shell_timeout_sec);
  claim->set_shell_net(s.shell_net);
  claim->set_read_only(s.read_only);
  claim->set_allow_unsandboxed_shell(s.allow_unsandboxed_shell);
  queue_claim_locked(std::move(msg));

  busy_task_ = task->id;
  busy_dialog_ = task->dialog_id;
  busy_since_ = last_event_ = Clock::now();
  cancel_deadline_.reset();
  action_started_.clear();
  if (auto claimed = db_->get_task(task->id)) {
    audit::open_task(*claimed, worker_id_, s.llm_model);
  }
  log::info("orch", "claim " + log::kv("task", static_cast<long long>(task->id)) + " " +
                        log::kv("dialog", static_cast<long long>(task->dialog_id)) + " " +
                        log::kv("turn", static_cast<long long>(task->turn_id)) + " " + log::kv("worker", worker_id_));
  announce_queue_locked(out);
  cv_.notify_all();
  return true;
}

void OrchestratorHub::dispatch_next() {
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    dispatch_locked(deferred);
  }
  run(deferred);
}

void OrchestratorHub::finish_busy_locked(std::int64_t task_id, const std::string& status, const std::string& error,
                                         const v1::TurnEvent* final_event, Deferred& out) {
  v1::TurnEvent terminal = final_event ? *final_event : v1::TurnEvent{};
  const bool changed = db_ && db_->finish_task(task_id, status, error, &terminal);
  // Already terminal (e.g. Done after a pre-marked cancel): close waiters, record nothing more.
  if (changed) {
    relay_locked(task_id, terminal, true, out);
  } else {
    relay_locked(task_id, v1::TurnEvent(), true, out);
  }
  if (changed && db_) {
    if (auto t = db_->get_task(task_id)) {
      audit::close_task(*t);
      log::info("orch", "finish " + log::kv("task", static_cast<long long>(task_id)) + " " +
                            log::kv("status", status) + (error.empty() ? "" : " " + log::kv("error", error)));
    }
  }
  abandon_interactions_locked(task_id);
  if (busy_task_ == task_id) {
    abandon_claim_locked();
    busy_task_ = 0;
    pending_confirm_.clear();
    pending_ask_ = false;
    busy_dialog_ = 0;
    cancel_deadline_.reset();
    action_started_.clear();
  }
  cancel_sent_.erase(task_id);
  queue_positions_.erase(task_id);
  cv_.notify_all();
}

}  // namespace arli
