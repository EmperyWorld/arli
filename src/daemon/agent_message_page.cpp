#include "agent_service.hpp"
#include "redact.hpp"

namespace arli {
grpc::Status AgentService::ListMessages(grpc::ServerContext*,const v1::ListMessagesRequest* req,v1::ListMessagesReply* reply) {
  if(req->dialog_id()<=0||req->before_id()<0||req->page_size()<0||req->page_size()>100)
    return {grpc::StatusCode::INVALID_ARGUMENT,"invalid message page"};
  if(!db_->get_dialog(req->dialog_id()))return {grpc::StatusCode::NOT_FOUND,"dialog not found"};
  try {
    std::int64_t next=0;
    for(const auto& m:db_->message_page(req->dialog_id(),req->before_id(),req->page_size()?req->page_size():50,next)) {
      auto* out=reply->add_messages();out->set_id(m.id);out->set_role(m.role);out->set_text(m.text);
      out->set_created_at(m.created_at);out->set_turn_id(m.turn_id);
    }
    reply->set_next_before_id(next);redact::message(*reply);
    if(reply->ByteSizeLong()>3*1024*1024)return {grpc::StatusCode::RESOURCE_EXHAUSTED,"MESSAGE_PAGE_TOO_LARGE"};
    return grpc::Status::OK;
  }catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}
  catch(const std::exception&){return {grpc::StatusCode::INTERNAL,"message history unavailable"};}
}
} // namespace arli
