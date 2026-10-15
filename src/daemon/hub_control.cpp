// OrchestratorHub — worker messages, cancel/stop, watchdog hooks.
#include "hub.hpp"

#include "log.hpp"

namespace arli {
namespace {

// Copy the relayable payload of a worker frame into a TurnEvent. False for control frames.
bool payload_to_event(const v1::WorkerToOrch& msg, v1::TurnEvent* ev) {
  if (msg.has_thought()) {
    *ev->mutable_thought() = msg.thought();
  } else if (msg.has_token()) {
    *ev->mutable_token() = msg.token();
  } else if (msg.has_action()) {
    *ev->mutable_action() = msg.action();
  } else if (msg.has_observation()) {
    *ev->mutable_observation() = msg.observation();
  } else if (msg.has_ask_user()) {
    *ev->mutable_ask_user() = msg.ask_user();
  } else if (msg.has_status()) {
    *ev->mutable_status() = msg.status();
  } else if (msg.has_error()) {
    *ev->mutable_error() = msg.error();
  } else if (msg.has_confirm_needed()) {
    *ev->mutable_confirm_needed() = msg.confirm_needed();
  } else if (msg.has_file_pass()) {
    *ev->mutable_file_pass() = msg.file_pass();
  } else if (msg.has_context_use()) {
    *ev->mutable_context_use() = msg.context_use();
  } else if (msg.has_plan()) {
    *ev->mutable_plan() = msg.plan();
  } else if (msg.has_memory()) {
    *ev->mutable_memory() = msg.memory();
  } else {
    return false;
  }
  return true;
}

}  // namespace

// ----- worker messages -----

bool OrchestratorHub::on_worker_message(std::uint64_t generation, const v1::WorkerToOrch& msg) {
  if (msg.has_command_ack()) return acknowledge_claim(generation, msg.command_ack());
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    if (!session_live_ || generation != session_generation_) return false;
    if (worker_id_.empty() && !msg.has_hello()) return false;
    if (msg.payload_case() == v1::WorkerToOrch::PAYLOAD_NOT_SET) return false;
    const std::int64_t task_id = msg.task_id() ? msg.task_id() : busy_task_;
    const std::int64_t dialog_id = msg.dialog_id();
    const std::int64_t turn_id = msg.turn_id();
    if (!msg.has_hello() && !msg.has_heartbeat()) {
      const auto task = db_ ? db_->get_task(task_id) : std::nullopt;
      if (!session_live_ || !busy_task_ || msg.task_id() != busy_task_ || !task ||
          dialog_id != task->dialog_id || turn_id != task->turn_id) return false;
      last_event_ = Clock::now();
    }

    if (msg.has_hello()) {
      const auto& hello = msg.hello();
      if (!worker_id_.empty() || hello.worker_id().empty() || hello.worker_id().size() > 128 ||
          hello.pid() <= 0 || msg.task_id() || dialog_id || turn_id) return false;
      for (const unsigned char c : hello.worker_id()) if (c < 33 || c > 126) return false;
      dialog_instructions_supported_ = hello.dialog_instructions_supported();
      mutation_revert_supported_ = hello.mutation_revert_supported();
      mutation_catalog_supported_ = hello.mutation_catalog_supported();workspace_identity_supported_=hello.workspace_identity_supported();
      interaction_ack_supported_ = hello.interaction_ack_supported();
      claim_ack_supported_ = hello.claim_ack_supported();
      worker_id_ = msg.hello().worker_id();
      worker_starting_ = false;
      log::info("orch", "worker hello " + log::kv("worker", worker_id_) + " " + log::kv("pid", msg.hello().pid()));
      cv_.notify_all();
      dispatch_locked(deferred);
    } else if (msg.has_heartbeat()) {
      // Heartbeats can race Done/next Claim on the worker's independent sender thread.
      // Drop stale heartbeats without touching a different task or closing a healthy stream.
      if (busy_task_) {
        const auto task = db_ ? db_->get_task(busy_task_) : std::nullopt;
        if (!task || msg.task_id() != busy_task_ || dialog_id != task->dialog_id ||
            turn_id != task->turn_id) return true;
      } else if (msg.task_id() || dialog_id || turn_id) return true;
      if (db_ && task_id) {
        db_->touch_task(task_id);
      }
    } else if (msg.has_claim_rejected()) {
      log::warn("orch", "claim rejected " + log::kv("task", static_cast<long long>(task_id)) + " " +
                            log::kv("reason", msg.claim_rejected().reason()));
      if (db_) {
        db_->requeue_task(task_id);
      }
      if (busy_task_ == task_id) {
        busy_task_ = 0;
      pending_confirm_.clear(); pending_ask_ = false;
        busy_dialog_ = 0;
      }
      announce_queue_locked(deferred);
    } else if (msg.has_task_failed()) {
      const auto ev = error_event(dialog_id, turn_id, msg.task_failed().message());
      finish_busy_locked(task_id, "failed", msg.task_failed().message(), &ev, deferred);
      dispatch_locked(deferred);
    } else if (msg.has_done()) {
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      *ev.mutable_done() = msg.done();
      const bool cancelled = cancel_sent_.count(task_id) > 0;
      const auto outcome = msg.done().outcome();
      const std::string status = cancelled || outcome == "cancelled" ? "cancelled" :
          outcome.empty() || outcome == "done" ? "done" : "failed";
      finish_busy_locked(task_id, status, status == "done" ? "" : msg.done().error_code(), &ev, deferred);
      dispatch_locked(deferred);
    } else {
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      if (!payload_to_event(msg, &ev)) {
        return false;
      }
      if (msg.has_observation() && db_ && task_id) {
        db_->touch_task(task_id);
      }
      if (msg.has_observation()) { pending_confirm_.clear(); pending_ask_ = false; }
      if (msg.has_confirm_needed()) pending_confirm_ = msg.confirm_needed().confirm_token();
      if (msg.has_ask_user()) { pending_ask_ = true; pending_question_id_ = msg.ask_user().question_id(); }
      relay_locked(task_id, ev, false, deferred);
    }
  }
  run(deferred);
  return true;
}

// ----- cancel / stop -----

bool OrchestratorHub::cancel_dialog(std::int64_t dialog_id) {
  Deferred deferred;
  bool handled = false;
  {
    std::lock_guard lock(mu_);
    auto t = db_ ? db_->active_task_for_dialog(dialog_id) : std::nullopt;
    if (!t) {
      return false;
    }
    handled = true;
    if (t->status == "queued") {
      const auto ev = error_event(t->dialog_id, t->turn_id, "отменено");
      finish_busy_locked(t->id, "cancelled", "cancel_before_claim", &ev, deferred);
      announce_queue_locked(deferred);
    } else if (cancel_sent_.insert(t->id).second) {
      v1::OrchToWorker msg;
      msg.mutable_cancel()->set_task_id(t->id);
      msg.mutable_cancel()->set_dialog_id(dialog_id);
      if (session_live_) {
        outbound_.push_back(std::move(msg));
      }
      cancel_deadline_ = Clock::now() + std::chrono::seconds(cancel_grace_sec_);
      log::info("orch", "cancel " + log::kv("task", static_cast<long long>(t->id)) + " " +
                            log::kv("grace_sec", static_cast<long long>(cancel_grace_sec_)));
      cv_.notify_all();
    }
  }
  run(deferred);
  return handled;
}

bool OrchestratorHub::hard_stop_dialog(std::int64_t dialog_id) {
  Deferred deferred;
  bool handled = false;
  {
    std::lock_guard lock(mu_);
    auto t = db_ ? db_->active_task_for_dialog(dialog_id) : std::nullopt;
    if (!t) {
      return false;
    }
    handled = true;
    const auto ev = error_event(t->dialog_id, t->turn_id, "остановлено: воркер перезапущен");
    finish_busy_locked(t->id, "cancelled", "hard_stop", &ev, deferred);
    if (t->status == "running") {
      request_kill_locked("hard stop");
    }
    announce_queue_locked(deferred);
  }
  run(deferred);
  return handled;
}

// ----- watchdog hooks -----

void OrchestratorHub::request_kill_locked(const std::string& reason) {
  worker_doomed_ = true;
  if (!kill_request_) {
    kill_request_ = KillRequest{reason};
    log::warn("orch", "kill worker requested: " + reason);
  }
}

std::optional<OrchestratorHub::KillRequest> OrchestratorHub::take_kill_request() {
  std::lock_guard lock(mu_);
  auto k = kill_request_;
  kill_request_.reset();
  return k;
}

void OrchestratorHub::tick(Clock::time_point now) {
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    retry_interactions_locked(now, deferred);
    retry_claim_locked(now, deferred);
    if (busy_task_ != 0) {
      const auto since_event = std::chrono::duration_cast<std::chrono::seconds>(now - last_event_).count();
      const auto since_start = std::chrono::duration_cast<std::chrono::seconds>(now - busy_since_).count();
      auto t = db_ ? db_->get_task(busy_task_) : std::nullopt;
      const std::int64_t turn_id = t ? t->turn_id : 0;
      if (cancel_deadline_ && now > *cancel_deadline_) {
        const auto ev = error_event(busy_dialog_, turn_id, "отменено: воркер не ответил, перезапуск");
        finish_busy_locked(busy_task_, "cancelled", "cancel_grace", &ev, deferred);
        request_kill_locked("cancel grace expired");
      } else if (idle_timeout_sec_ > 0 && since_event > idle_timeout_sec_) {
        const auto ev = error_event(busy_dialog_, turn_id, "таймаут: нет событий от воркера");
        finish_busy_locked(busy_task_, "failed", "idle_timeout", &ev, deferred);
        request_kill_locked("idle timeout");
      } else if (turn_timeout_sec_ > 0 && since_start > turn_timeout_sec_) {
        const auto ev = error_event(busy_dialog_, turn_id, "таймаут хода");
        finish_busy_locked(busy_task_, "failed", "turn_timeout", &ev, deferred);
        request_kill_locked("turn timeout");
      }
    }
    if (busy_task_ == 0 && !kill_request_) {
      dispatch_locked(deferred);
    }
  }
  run(deferred);
}

void OrchestratorHub::fail_queued(const std::string& reason) {
  Deferred deferred;
  {
    std::lock_guard lock(mu_);
    if (!db_) {
      return;
    }
    while (auto t = db_->next_queued()) {
      const auto ev = error_event(t->dialog_id, t->turn_id, reason);
      finish_busy_locked(t->id, "failed", reason, &ev, deferred);
    }
  }
  run(deferred);
}

bool OrchestratorHub::shutting_down() const {
  std::lock_guard lock(mu_);
  return shutting_down_;
}

void OrchestratorHub::request_shutdown() {
  std::lock_guard lock(mu_);
  shutting_down_ = true;
  if (session_live_) {
    v1::OrchToWorker msg;
    msg.mutable_shutdown();
    outbound_.push_back(std::move(msg));
  }
  cv_.notify_all();
}

}  // namespace arli
