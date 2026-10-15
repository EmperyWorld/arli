#include "redact.hpp"
// OrchestratorHub — relay point: record every event (steps + audit), fan out to UI waiters.
#include "hub.hpp"

#include "audit.hpp"
#include "log.hpp"
#include "trace.hpp"

namespace arli {

bool OrchestratorHub::interaction_active(std::int64_t task, const v1::TurnEvent& event) const {
  std::lock_guard lock(mu_);
  if (!session_live_ || task != busy_task_ || cancel_sent_.count(task)) return false;
  if (event.has_ask_user()) return pending_ask_ && event.ask_user().question_id() == pending_question_id_;
  if (event.has_confirm_needed()) return !pending_confirm_.empty() && event.confirm_needed().confirm_token() == pending_confirm_;
  return false;
}

void OrchestratorHub::schedule_locked(std::int64_t waiter_id, Waiter& w, const v1::TurnEvent& ev, bool final,
                                      Deferred& out) {
  ++w.in_flight;
  out.push_back([this, waiter_id, fn = w.fn, ev, final] {
    fn(ev);
    std::lock_guard lock(mu_);
    if (auto it = waiters_.find(waiter_id); it != waiters_.end()) {
      --it->second.in_flight;
      if (final) {
        it->second.done = true;
      }
    }
    cv_.notify_all();
  });
}

void OrchestratorHub::record_locked(std::int64_t task_id, const v1::TurnEvent& ev) {
  if (!db_ || task_id == 0) {
    return;
  }
  if (ev.has_file_pass()) {
    std::vector<std::string> paths(ev.file_pass().paths().begin(), ev.file_pass().paths().end());
    std::vector<std::string> done(ev.file_pass().done().begin(), ev.file_pass().done().end());
    std::vector<std::string> reviewed(ev.file_pass().reviewed().begin(), ev.file_pass().reviewed().end());
    db_->set_file_pass(ev.dialog_id(), paths, done, reviewed);
  }
  if (ev.has_plan()) {
    const auto& p = ev.plan();
    std::vector<std::string> steps(p.steps().begin(), p.steps().end());
    db_->set_plan(ev.dialog_id(), steps, p.current(), p.current_failed());
  }
  if (ev.has_memory()) {
    std::vector<MemoryRow> rows;
    rows.reserve(static_cast<size_t>(ev.memory().entries_size()));
    for (const auto& e : ev.memory().entries()) {
      MemoryRow row;
      row.id = e.id();
      row.text = e.text();
      rows.push_back(std::move(row));
    }
    db_->replace_memory(rows);
  }
  auto step = trace::step_from_event(ev, task_id);
  if (!step) {
    return;
  }
  // Live Token deltas are for the UI bubble only — do not fill steps/audit.
  if (step->kind == "token") {
    return;
  }
  const auto now = Clock::now();
  if (step->kind == "action" && step->step_id) {
    action_started_[step->step_id] = now;
  } else if (step->kind == "observation" && step->step_id) {
    if (step->duration_ms <= 0) {
      if (auto it = action_started_.find(step->step_id); it != action_started_.end()) {
        step->duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
        action_started_.erase(it);
      }
    } else {
      action_started_.erase(step->step_id);
    }
  }
  try {
    step->id = db_->add_step(*step);
  } catch (const std::exception& e) {
    log::warn("orch", std::string("add_step failed: ") + e.what());
  }
  audit::append_event(task_id, *step);
  if (step->kind == "observation" || step->kind == "error") {
    log::info("orch", log::kv("task", static_cast<long long>(task_id)) + " " + log::kv("kind", step->kind) +
                          (step->tool.empty() ? "" : " " + log::kv("tool", step->tool)) + " " +
                          log::kv("ok", step->ok ? 1LL : 0LL) + " " +
                          log::kv("dur_ms", static_cast<long long>(step->duration_ms)));
  }
}

void OrchestratorHub::relay_locked(std::int64_t task_id, const v1::TurnEvent& ev, bool final, Deferred& out) {
  auto durable = ev;
  redact::message(durable);
  const bool has_payload = ev.payload_case() != v1::TurnEvent::PAYLOAD_NOT_SET;
  if (has_payload) {
    durable.set_task_id(task_id);
    if (!durable.event_id()) durable.set_event_id(db_->append_event(task_id, durable));
    record_locked(task_id, durable);
  }
  for (auto& [wid, w] : waiters_) {
    if (w.task_id != task_id || w.closing) {
      continue;
    }
    if (final) {
      w.closing = true;
    }
    if (has_payload && w.fn) {
      schedule_locked(wid, w, durable, final, out);
    } else if (final) {
      w.done = true;
    }
  }
}

std::int64_t OrchestratorHub::register_waiter(std::int64_t task_id, EventFn fn, std::vector<StepRow>* replay) {
  Deferred deferred;
  std::int64_t id = 0;
  {
    std::lock_guard lock(mu_);
    id = next_waiter_id_++;
    Waiter w;
    w.task_id = task_id;
    w.fn = std::move(fn);
    if (replay && db_) {
      *replay = db_->list_steps_for_task(task_id);
    }
    if (db_) {
      if (auto t = db_->get_task(task_id); !t || task_status_terminal(t->status)) {
        w.closing = true;
        w.done = true;
      }
    }
    waiters_[id] = std::move(w);
    announce_queue_locked(deferred);
  }
  run(deferred);
  return id;
}

void OrchestratorHub::unregister_waiter(std::int64_t waiter_id) {
  std::unique_lock lock(mu_);
  // Detach, then wait for callbacks that still hold the caller's writer.
  auto it = waiters_.find(waiter_id);
  if (it == waiters_.end()) {
    return;
  }
  it->second.closing = true;
  it->second.fn = nullptr;
  cv_.wait(lock, [&] {
    auto w = waiters_.find(waiter_id);
    return w == waiters_.end() || w->second.in_flight == 0;
  });
  waiters_.erase(waiter_id);
  cv_.notify_all();
}

bool OrchestratorHub::wait_task_finished(std::int64_t waiter_id, int timeout_ms) {
  std::unique_lock lock(mu_);
  return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
    auto it = waiters_.find(waiter_id);
    return it == waiters_.end() || (it->second.done && it->second.in_flight == 0);
  });
}

void OrchestratorHub::announce_queue_locked(Deferred& out) {
  if (!db_) {
    return;
  }
  // Position 1 with an idle, ready worker is dispatched right away: no need to announce it.
  const bool idle_ready =
      session_live_ && !worker_id_.empty() && busy_task_ == 0 && !shutting_down_ && !worker_doomed_;
  for (auto& [wid, w] : waiters_) {
    if (w.closing || !w.fn) {
      continue;
    }
    const int pos = db_->queue_position(w.task_id);
    if (pos <= 0 || (pos == 1 && idle_ready)) {
      continue;
    }
    auto t = db_->get_task(w.task_id);
    v1::TurnEvent ev;
    ev.set_dialog_id(t ? t->dialog_id : 0);
    ev.set_turn_id(t ? t->turn_id : 0);
    ev.mutable_queued()->set_position(pos);
    if (queue_positions_[w.task_id] != pos) {
      queue_positions_[w.task_id] = pos;
      ev.set_task_id(w.task_id);
      ev.set_event_id(db_->append_event(w.task_id, ev));
    }
    schedule_locked(wid, w, ev, false, out);
  }
}

}  // namespace arli
