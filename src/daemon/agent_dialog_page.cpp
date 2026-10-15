#include "agent_service.hpp"
#include "agent_service_fill.hpp"
#include "redact.hpp"
namespace arli {
grpc::Status AgentService::ListDialogPage(grpc::ServerContext*,const v1::ListDialogPageRequest* req,v1::ListDialogPageReply* reply) {
  if(req->page_size()<0||req->page_size()>100||req->page_token().size()>512)
    return {grpc::StatusCode::INVALID_ARGUMENT,"invalid dialog page"};
  try {
    std::string next;
    for(const auto& row:db_->dialog_page(req->page_token(),req->page_size()?req->page_size():50,next))fill_dialog(row,reply->add_dialogs());
    reply->set_next_page_token(next);redact::message(*reply);
    if(reply->ByteSizeLong()>3*1024*1024)return {grpc::StatusCode::RESOURCE_EXHAUSTED,"DIALOG_PAGE_TOO_LARGE"};
    return grpc::Status::OK;
  }catch(const std::invalid_argument& e){return {grpc::StatusCode::INVALID_ARGUMENT,e.what()};}
  catch(const std::domain_error& e){return {grpc::StatusCode::ABORTED,e.what()};}
  catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}
  catch(...){return {grpc::StatusCode::INTERNAL,"dialog listing unavailable"};}
}
} // namespace arli
