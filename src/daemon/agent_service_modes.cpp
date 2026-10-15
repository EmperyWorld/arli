#include "redact.hpp"
#include "agent_service.hpp"

namespace arli {

using grpc::ServerContext;
using grpc::Status;

Status AgentService::GetFilePass(ServerContext*, const v1::GetFilePassRequest* req, v1::GetFilePassReply* reply) {
  if (!db_->get_dialog(req->dialog_id())) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  if (const auto row = db_->get_file_pass(req->dialog_id())) {
    for (const auto& p : row->paths) {
      reply->add_paths(p);
    }
    for (const auto& d : row->done) {
      reply->add_done(d);
    }
    for (const auto& p : row->reviewed) {
      reply->add_reviewed(p);
    }
  }
  redact::message(*reply);return Status::OK;
}

Status AgentService::ClearFilePass(ServerContext*, const v1::ClearFilePassRequest* req, v1::ClearFilePassReply* reply) {
  if (!db_->get_dialog(req->dialog_id())) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  db_->clear_file_pass(req->dialog_id());
  reply->set_ok(true);
  redact::message(*reply);return Status::OK;
}

Status AgentService::GetPlan(ServerContext*, const v1::GetPlanRequest* req, v1::GetPlanReply* reply) {
  if (!db_->get_dialog(req->dialog_id())) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  if (const auto row = db_->get_plan(req->dialog_id())) {
    for (const auto& s : row->steps) {
      reply->add_steps(s);
    }
    reply->set_current(row->current);
    reply->set_current_failed(row->failed);
  }
  redact::message(*reply);return Status::OK;
}

Status AgentService::ClearPlan(ServerContext*, const v1::ClearPlanRequest* req, v1::ClearPlanReply* reply) {
  if (!db_->get_dialog(req->dialog_id())) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  db_->clear_plan(req->dialog_id());
  reply->set_ok(true);
  redact::message(*reply);return Status::OK;
}

}  // namespace arli
