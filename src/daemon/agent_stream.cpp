#include "agent_service.hpp"
#include "workspace_identity.hpp"
#include "hub.hpp"
#include "turn_stream_guard.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
namespace arli {
using grpc::Status;
using grpc::StatusCode;
Status AgentService::stream_task(grpc::ServerContext* ctx, std::int64_t task,
    std::int64_t after, grpc::ServerWriter<v1::TurnEvent>* writer) try {
  if (!db_->valid_event_cursor(task, after)) return Status(StatusCode::INVALID_ARGUMENT, "event cursor does not belong to task");
  auto& hub = OrchestratorHub::instance();
  const auto waiter = hub.register_waiter(task, [](const auto&) { return true; });
  struct Detach { OrchestratorHub& hub; std::int64_t id; ~Detach() { hub.unregister_waiter(id); } } detach{hub, waiter};
  TurnStreamWriter output(ctx,writer);
  hub.dispatch_next();
  bool journal_seen = after > 0;
  // The database is the stream source. A slow consumer cannot block hub callbacks
  // or accumulate an unbounded per-client queue. Each page contains at most 64 events.
  while (!ctx->IsCancelled()) {
    const bool finished = hub.wait_task_finished(waiter, 20);
    const auto page = db_->events_after(task, after);
    journal_seen |= !page.empty();
    for (auto event : page) {
      event.set_interaction_active(hub.interaction_active(task, event));
      if (!output.write(event)) return Status::CANCELLED;
      after = event.event_id();
    }
    if (finished && page.empty()) return journal_seen ? Status::OK :
        Status(StatusCode::FAILED_PRECONDITION, "legacy task has no event journal; use OpenDialog/GetTurnTrace");
  }
  return Status::CANCELLED;
} catch (const std::exception&) { return Status(StatusCode::INTERNAL, "event storage unavailable"); }
Status AgentService::Send(grpc::ServerContext* ctx, const v1::SendRequest* req, grpc::ServerWriter<v1::TurnEvent>* writer) try {
  if (req->text().empty() || req->text().find('\0') != std::string::npos || req->after_event_id() < 0 || req->request_id().size() > 128 ||
      std::any_of(req->request_id().begin(), req->request_id().end(), [](unsigned char c) { return c < 33 || c > 126; }))
    return Status(StatusCode::INVALID_ARGUMENT, "invalid send request");
  if (req->text().size() > 256 * 1024) return Status(StatusCode::RESOURCE_EXHAUSTED, "message too large");
  TurnStreamSlot slot(turn_streams_);
  TaskRow task;
  {
    std::lock_guard lock(enqueue_mu_);
    if (!db_->get_dialog(req->dialog_id())) return Status(StatusCode::NOT_FOUND, "dialog not found");
    if (auto existing = db_->task_by_request(req->dialog_id(), req->request_id())) {
      if ((!existing->config_json.empty() && nlohmann::json::parse(existing->config_json).contains("revert_changeset_id")) || existing->text != req->text()) return Status(StatusCode::ALREADY_EXISTS, "request_id has different text");
      task = *existing;
    } else {
      if (req->after_event_id() != 0) return Status(StatusCode::INVALID_ARGUMENT, "new request cannot have an event cursor");
      if (db_->active_task_for_dialog(req->dialog_id())) return Status(StatusCode::FAILED_PRECONDITION, "another turn is active for dialog");
      task = db_->enqueue_task(req->dialog_id(), db_->next_turn_id(req->dialog_id()), req->text(), req->request_id());
    }
  }
  return stream_task(ctx, task.id, req->after_event_id(), writer);
} catch (const TaskLimitError& e) { return Status(StatusCode::RESOURCE_EXHAUSTED,e.what()); } catch (const WorkspaceError& e) { return Status(StatusCode::FAILED_PRECONDITION,e.what()); } catch (const std::exception&) { return Status(StatusCode::INTERNAL, "event storage unavailable"); }
Status AgentService::AttachTurn(grpc::ServerContext* ctx, const v1::AttachTurnRequest* req, grpc::ServerWriter<v1::TurnEvent>* writer) try {
  if (req->after_event_id() < 0 || req->task_id() < 0) return Status(StatusCode::INVALID_ARGUMENT, "invalid event cursor");
  TurnStreamSlot slot(turn_streams_);
  const auto task = req->task_id() ? db_->get_task(req->task_id()) : db_->active_task_for_dialog(req->dialog_id());
  if (!task || task->dialog_id != req->dialog_id()) return Status(StatusCode::NOT_FOUND, "task not found for dialog");
  return stream_task(ctx, task->id, req->after_event_id(), writer);
} catch (const TaskLimitError& e) { return Status(StatusCode::RESOURCE_EXHAUSTED,e.what()); } catch (const std::exception&) { return Status(StatusCode::INTERNAL, "event storage unavailable"); }
}
