#include "redact.hpp"
#include "agent_service.hpp"

#include "agent_service_fill.hpp"
#include "hub.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "llm_models.hpp"
#include "llm_sampling.hpp"
#include "spawn_agent.hpp"
#include "trace.hpp"
#include "mutation_catalog.hpp"
#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace arli {

using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

Status AgentService::Health(ServerContext*, const v1::HealthRequest*, v1::HealthReply* reply) {
  auto& hub = OrchestratorHub::instance();
  reply->set_serving(true);
  reply->set_event_resume_supported(true);
  reply->set_slice(kSlice);
  reply->set_version(kVersion);
  reply->set_worker_state(hub.worker_state());
  reply->set_worker_pid(agent_pid());
  reply->set_queue_depth(hub.queue_depth());
  reply->set_llm_reachable(llm_reachable_.load());
  redact::message(*reply);return Status::OK;
}

Status AgentService::ListDialogs(ServerContext*, const v1::ListDialogsRequest*, v1::ListDialogsReply* reply) {
  try {
  for (const auto& d : db_->list_dialogs()) {
    fill_dialog(d, reply->add_dialogs());
  }
  redact::message(*reply);
  if(reply->ByteSizeLong()>3*1024*1024)return {grpc::StatusCode::RESOURCE_EXHAUSTED,"DIALOG_LIST_TOO_LARGE: use ListDialogPage"};
  return Status::OK;
  }catch(const std::length_error& e){return {grpc::StatusCode::RESOURCE_EXHAUSTED,e.what()};}
  catch(...){return {grpc::StatusCode::INTERNAL,"dialog listing unavailable"};}
}

Status AgentService::CreateDialog(ServerContext*, const v1::CreateDialogRequest* req, v1::Dialog* reply) {
  if(req->title().size()>4096||req->title().find('\0')!=std::string::npos)
    return {grpc::StatusCode::INVALID_ARGUMENT,"dialog title exceeds 4 KiB or contains NUL"};
  fill_dialog(db_->create_dialog(req->title()), reply);
  redact::message(*reply);return Status::OK;
}

Status AgentService::SetDialogInstructions(ServerContext*, const v1::SetDialogInstructionsRequest* req, v1::Dialog* reply) {
  if (req->instructions().size() > 16384 || req->instructions().find('\0') != std::string::npos)
    return Status(grpc::StatusCode::INVALID_ARGUMENT, "instructions exceed 16 KiB or contain NUL");
  std::lock_guard lock(enqueue_mu_);
  if (!db_->set_dialog_instructions(req->dialog_id(), req->instructions()))
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  fill_dialog(*db_->get_dialog(req->dialog_id()), reply);
  redact::message(*reply);return Status::OK;
}

Status AgentService::RenameDialog(ServerContext*, const v1::RenameDialogRequest* req, v1::Dialog* reply) {
  if(req->title().size()>4096||req->title().find('\0')!=std::string::npos)
    return {grpc::StatusCode::INVALID_ARGUMENT,"dialog title exceeds 4 KiB or contains NUL"};
  if (!db_->rename_dialog(req->dialog_id(), req->title())) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  const auto d = db_->get_dialog(req->dialog_id());
  if (!d) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  fill_dialog(*d, reply);
  redact::message(*reply);return Status::OK;
}

Status AgentService::DeleteDialog(ServerContext*, const v1::DeleteDialogRequest* req, v1::DeleteDialogReply* reply) {
  std::lock_guard lock(enqueue_mu_);
  if (db_->active_task_for_dialog(req->dialog_id()))
    return Status(grpc::StatusCode::FAILED_PRECONDITION, "dialog has an active task");
  reply->set_ok(db_->delete_dialog(req->dialog_id()));
  redact::message(*reply);return Status::OK;
}

Status AgentService::OpenDialog(ServerContext* ctx, const v1::OpenDialogRequest* req, v1::OpenDialogReply* reply) {
  if(req->message_page_size()<0||req->message_page_size()>100)
    return {grpc::StatusCode::INVALID_ARGUMENT,"invalid message page size"};
  const auto d = db_->get_dialog(req->dialog_id());
  if (!d) {
    return Status(grpc::StatusCode::NOT_FOUND, "dialog not found");
  }
  fill_dialog(*d, reply->mutable_dialog());
  if(req->message_page_size()>0){
    v1::ListMessagesRequest q;q.set_dialog_id(d->id);q.set_page_size(req->message_page_size());
    v1::ListMessagesReply page;const auto status=ListMessages(ctx,&q,&page);if(!status.ok())return status;
    reply->mutable_messages()->Swap(page.mutable_messages());reply->set_next_before_id(page.next_before_id());
  } else for (const auto& m : db_->list_messages(d->id)) {
    auto* out = reply->add_messages();
    out->set_id(m.id);
    out->set_role(m.role);
    out->set_text(m.text);
    out->set_created_at(m.created_at);
    out->set_turn_id(m.turn_id);
  }
  if (const std::int64_t turn = db_->last_traced_turn(d->id); turn > 0) {
    v1::GetTurnTraceRequest trace_request;trace_request.set_dialog_id(d->id);trace_request.set_turn_id(turn);trace_request.set_page_size(100);
    v1::GetTurnTraceReply trace_reply;const auto status=GetTurnTrace(ctx,&trace_request,&trace_reply);if(!status.ok())return status;
    reply->mutable_last_trace()->Swap(trace_reply.mutable_items());
    reply->set_last_trace_next_after_id(trace_reply.next_after_id());
  }
  redact::message(*reply);return Status::OK;
}

Status AgentService::GetSettings(ServerContext*, const v1::GetSettingsRequest*, v1::Settings* reply) {
  fill_settings(db_->get_settings(), reply);
  redact::message(*reply);return Status::OK;
}

Status AgentService::SetSettings(ServerContext*, const v1::Settings* req, v1::Settings* reply) {
  if(req->ByteSizeLong()>512*1024)return {grpc::StatusCode::RESOURCE_EXHAUSTED,"SETTINGS_TOO_LARGE: maximum is 512 KiB"};
  auto invalid_text=[](const std::string& value,std::size_t limit){return value.size()>limit||value.find('\0')!=std::string::npos;};
  if(invalid_text(req->llm_base_url(),4096)||invalid_text(req->llm_model(),4096)||
     invalid_text(req->workdir(),4096)||invalid_text(req->llm_profile(),64)||
     invalid_text(req->llm_api_key(),65536)||invalid_text(req->shell_auto(),65536)||
     invalid_text(req->shell_ask(),65536)||invalid_text(req->list_ignore(),65536))
    return {grpc::StatusCode::INVALID_ARGUMENT,"settings field exceeds its limit or contains NUL"};
  if((req->context_tokens()>0&&(req->context_tokens()<1024||req->context_tokens()>262144))||
     (req->max_steps()>0&&(req->max_steps()<1||req->max_steps()>256))||
     req->turn_timeout_sec()>86400||req->idle_timeout_sec()>86400||req->cancel_grace_sec()>600||
     req->shell_timeout_sec()>3600)
    return {grpc::StatusCode::INVALID_ARGUMENT,"numeric setting is outside the supported range"};
  if (req->has_llm_temperature() && !valid_llm_temperature(req->llm_temperature()))
    return {grpc::StatusCode::INVALID_ARGUMENT, "llm_temperature must be -1 (profile default) or finite 0..2"};
  std::lock_guard lock(settings_mu_);
  SettingsRow in = db_->get_settings();
  in.llm_base_url = req->llm_base_url();
  in.llm_model = req->llm_model();
  in.workdir = req->workdir();
  if (req->clear_api_key()) {
    in.llm_api_key.clear();
  } else if (!req->llm_api_key().empty()) {
    in.llm_api_key = req->llm_api_key();
  }
  if (req->turn_timeout_sec() > 0) {
    in.turn_timeout_sec = req->turn_timeout_sec();
  }
  if (req->idle_timeout_sec() > 0) {
    in.idle_timeout_sec = req->idle_timeout_sec();
  }
  if (req->cancel_grace_sec() > 0) {
    in.cancel_grace_sec = req->cancel_grace_sec();
  }
  in.shell_auto = req->shell_auto();
  in.shell_ask = req->shell_ask();
  in.shell_allow_all = req->shell_allow_all();
  in.delete_confirm = req->delete_confirm();
  in.list_ignore = req->list_ignore();
  if (req->context_tokens() > 0) {
    in.context_tokens = req->context_tokens();
  }
  if (req->max_steps() > 0) {
    in.max_steps = req->max_steps();
  }
  if (!req->llm_profile().empty()) {
    in.llm_profile = normalize_llm_profile(req->llm_profile());
  }
  if (req->has_llm_temperature()) in.llm_temperature = req->llm_temperature();
  if (req->shell_timeout_sec() > 0) {
    in.shell_timeout_sec = req->shell_timeout_sec();
  }
  in.shell_net = req->shell_net();
  in.read_only = req->read_only();
  in.allow_unsandboxed_shell = req->allow_unsandboxed_shell();
  const SettingsRow saved = db_->set_settings(in);
  refresh_health();
  fill_settings(saved, reply);
  reply->set_llm_model_listed(probe_llm_endpoint(llm_config_from_settings(saved), saved.llm_model).model_listed);
  redact::message(*reply);return Status::OK;
}

Status AgentService::Cancel(ServerContext*, const v1::CancelRequest* req, v1::CancelReply* reply) {
  reply->set_ok(OrchestratorHub::instance().cancel_dialog(req->dialog_id()));
  redact::message(*reply);return Status::OK;
}

Status AgentService::HardStop(ServerContext*, const v1::HardStopRequest* req, v1::HardStopReply* reply) {
  const bool ok = OrchestratorHub::instance().hard_stop_dialog(req->dialog_id());
  reply->set_ok(ok);
  reply->set_detail(ok ? "worker restart requested" : "no active task");
  redact::message(*reply);return Status::OK;
}

Status AgentService::ListTasks(ServerContext*, const v1::ListTasksRequest* req, v1::ListTasksReply* reply) {
  for (const auto& t : db_->list_tasks(req->dialog_id(), req->limit())) {
    fill_task(t, reply->add_tasks());
  }
  redact::message(*reply);return Status::OK;
}

Status AgentService::AnswerAskUser(ServerContext*, const v1::AnswerAskUserRequest* req,
                                   v1::AnswerAskUserReply* reply) {
  std::int64_t id = 0;
  const bool ok =
      OrchestratorHub::instance().deliver_ask_answer(req->dialog_id(), req->answer(), req->question_id(), &id);
  reply->set_command_id(id);
  reply->set_ok(ok);
  redact::message(*reply);return Status::OK;
}

Status AgentService::ConfirmDanger(ServerContext*, const v1::ConfirmDangerRequest* req,
                                   v1::ConfirmDangerReply* reply) {
  std::int64_t id = 0;
  const bool ok = OrchestratorHub::instance().deliver_confirm(req->dialog_id(), req->confirm_token(), req->accept(), &id);
  reply->set_command_id(id);
  reply->set_ok(ok);
  reply->set_detail(ok ? "queued" : "no pending confirmation");
  redact::message(*reply);return Status::OK;
}

Status AgentService::GetTurnTrace(ServerContext*, const v1::GetTurnTraceRequest* req, v1::GetTurnTraceReply* reply) {
  if(req->after_id()<0||req->page_size()<0||req->page_size()>100)
    return {grpc::StatusCode::INVALID_ARGUMENT,"invalid trace cursor or page size"};
  const std::int64_t turn = req->turn_id() > 0 ? req->turn_id() : db_->last_traced_turn(req->dialog_id());
  reply->set_turn_id(turn);
  if (turn <= 0) {
    redact::message(*reply);return Status::OK;
  }
  bool has_more=false;const int limit=req->page_size()>0?req->page_size():50;
  const auto rows=db_->list_steps_for_turn_page(req->dialog_id(),turn,req->after_id(),limit,1024*1024,has_more);
  for (const auto& s : rows) {
    trace::fill_trace_item(s, reply->add_items());
  }
  if(has_more&&!rows.empty())reply->set_next_after_id(rows.back().id);
  if(has_more){redact::message(*reply);return Status::OK;}
  try {
    const auto task=db_->task_for_turn(req->dialog_id(),turn);
    if(task&&task_status_terminal(task->status)&&!task->config_json.empty()){
      const auto config=nlohmann::json::parse(task->config_json);
      std::size_t recovery_items=0;bool recovery_omitted=false;
      for(const auto& journal:task_journals(config.at("workdir").get<std::string>(),task->dialog_id,task->turn_id,task->id)){
        if(recovery_items>=8||reply->ByteSizeLong()>1536*1024){recovery_omitted=true;break;}
        const auto& id=journal.id;
        if(!journal.acceptance.is_null()){
          auto* item=reply->add_items();item->set_kind("acceptance");item->set_turn_id(turn);item->set_tool("acceptance_plan");
          item->set_text("Критерии, сохранённые до выполнения:\n"+journal.acceptance.dump(2));item->set_args_json(journal.acceptance.dump());
          ++recovery_items;
        }
        if(!journal.has_mutations)continue;
        bool present=false;for(const auto& item:reply->items())if(item.kind()=="done"){const auto d=nlohmann::json::parse(item.args_json(),nullptr,false);if(d.is_object()&&d.value("changesetId","")==id)present=true;}
        if(!present){
          if(recovery_items>=8||reply->ByteSizeLong()>1536*1024){recovery_omitted=true;break;}
          auto* item=reply->add_items();item->set_kind("changeset");item->set_turn_id(turn);item->set_text("Найден журнал прерванного хода");item->set_args_json(nlohmann::json{{"changesetId",id}}.dump());++recovery_items;
        }
      }
      if(recovery_omitted){auto* item=reply->add_items();item->set_kind("notice");item->set_turn_id(turn);item->set_text("Дополнительные аварийные журналы скрыты из ограниченного ответа.");}
    }
  }catch(...){auto* item=reply->add_items();item->set_kind("error");item->set_text("Поиск аварийного журнала недоступен; исходные данные сохранены.");}
  redact::message(*reply);return Status::OK;
}

}  // namespace arli
