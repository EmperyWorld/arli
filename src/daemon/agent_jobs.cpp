#include "redact.hpp"
#include "agent_service.hpp"

#include "jobs.hpp"

#include <signal.h>
#include <unistd.h>

namespace arli {

grpc::Status AgentService::ListJobs(grpc::ServerContext*, const v1::ListJobsRequest* request, v1::ListJobsReply* reply) {
  if(request->dialog_id()<0||request->task_id()<0)return {grpc::StatusCode::INVALID_ARGUMENT,"invalid job owner filter"};
  for (auto job : db_->list_jobs(40,request->dialog_id(),request->task_id())) {
    job = refresh_job(*db_, std::move(job));
    // Finished and stopped jobs leave the desk. The row stays in the database.
    if (job.status != "running" && !request->include_finished()) {
      continue;
    }
    auto* out = reply->add_jobs();
    out->set_id(job.id);
    out->set_dialog_id(job.dialog_id);out->set_task_id(job.task_id);out->set_turn_id(job.turn_id);
    out->set_pid(job.pid);
    out->set_command(job.command);
    out->set_workdir(job.workdir);
    out->set_status(job.status);
    out->set_started_at(job.started_at);
    out->set_finished_at(job.finished_at);
    out->set_log_tail(job_log_tail(job.log_path));
    out->set_exit_code(job.exit_code);
    out->set_exit_code_known(job.exit_code >= 0);
  }
  redact::message(*reply);return grpc::Status::OK;
}

grpc::Status AgentService::StopJob(grpc::ServerContext*, const v1::StopJobRequest* req, v1::StopJobReply* reply) {
  if(req->expected_dialog_id()<0||req->expected_task_id()<0)return {grpc::StatusCode::INVALID_ARGUMENT,"invalid expected job owner"};
  const auto job=db_->get_job(req->id());
  if(job&&((req->expected_dialog_id()&&job->dialog_id!=req->expected_dialog_id())||
           (req->expected_task_id()&&job->task_id!=req->expected_task_id()))){
    reply->set_ok(false);reply->set_detail("job ownership mismatch");return grpc::Status::OK;
  }
  const bool ok = stop_job_by_id(*db_, req->id());
  reply->set_ok(ok);
  reply->set_detail(ok ? "stopped" : "stop not completed or job no longer active");
  redact::message(*reply);return grpc::Status::OK;
}

}  // namespace arli
