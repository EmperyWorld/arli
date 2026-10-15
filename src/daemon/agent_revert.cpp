#include "agent_service.hpp"
#include "workspace_identity.hpp"
#include "turn_stream_guard.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
namespace arli {
grpc::Status AgentService::RevertChangeset(grpc::ServerContext* ctx,const v1::RevertChangesetRequest* req,grpc::ServerWriter<v1::TurnEvent>* writer) try {
  using grpc::Status;using grpc::StatusCode;
  const auto& id=req->changeset_id();const auto& request=req->request_id();
  if(id.size()!=37||id.rfind("turn-",0)!=0||id.substr(5).find_first_not_of("0123456789abcdef")!=std::string::npos||
     request.empty()||request.size()>128||req->after_event_id()<0||req->source_turn_id()<0||std::any_of(request.begin(),request.end(),[](unsigned char c){return c<33||c>126;}))
    return Status(StatusCode::INVALID_ARGUMENT,"invalid revert request");
  TurnStreamSlot slot(turn_streams_);TaskRow task;
  {
    std::lock_guard lock(enqueue_mu_);
    if(!db_->get_dialog(req->dialog_id()))return Status(StatusCode::NOT_FOUND,"dialog not found");
    if(auto prior=db_->task_by_request(req->dialog_id(),request)){
      if(prior->config_json.empty()||nlohmann::json::parse(prior->config_json).value("revert_changeset_id","")!=id||nlohmann::json::parse(prior->config_json).value("revert_source_turn",int64_t{0})!=req->source_turn_id())
        return Status(StatusCode::ALREADY_EXISTS,"request_id belongs to another operation");
      task=*prior;
    }else{
      if(req->after_event_id())return Status(StatusCode::INVALID_ARGUMENT,"new request cannot have an event cursor");
      if(db_->active_task_for_dialog(req->dialog_id()))return Status(StatusCode::FAILED_PRECONDITION,"another turn is active for dialog");
      const auto source=req->source_turn_id()?db_->task_for_turn(req->dialog_id(),req->source_turn_id()):std::nullopt;
      if(req->source_turn_id()&&(!source||!task_status_terminal(source->status)||source->config_json.empty()))return Status(StatusCode::FAILED_PRECONDITION,"source turn unavailable or active");
      task=db_->enqueue_task(req->dialog_id(),db_->next_turn_id(req->dialog_id()),"Откат изменений "+id,request,id,source?&*source:nullptr);
    }
  }
  return stream_task(ctx,task.id,req->after_event_id(),writer);
}catch(const TaskLimitError& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}catch(const WorkspaceError& e){return {grpc::StatusCode::FAILED_PRECONDITION,e.what()};}catch(const std::exception&){return {grpc::StatusCode::INTERNAL,"revert queue unavailable"};}
}
