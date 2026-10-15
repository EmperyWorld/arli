#include "worker_revert.hpp"
#include "tools/mutation_recovery.hpp"
#include "tools/mutation_revert.hpp"
#include "confirm.hpp"
#include "redact.hpp"
#include "workspace_identity.hpp"
#include <map>
namespace arli {
void run_revert(Database& db,const v1::ClaimTask& claim,EventSink sink,std::atomic<bool>* cancel,const std::function<void()>& check_workspace){
  auto event=[&]{v1::TurnEvent e;e.set_dialog_id(claim.dialog_id());e.set_turn_id(claim.turn_id());e.set_task_id(claim.task_id());return e;};
  std::string outcome="failed",detail,changeset;auto stopped=[&]{return cancel&&cancel->load();};
  db.add_message(claim.dialog_id(),"user",claim.user_text(),claim.turn_id());
  try{
    if(claim.read_only())throw std::runtime_error("read_only: откат отключён");
    if(stopped())throw std::runtime_error("отменено");
    const auto record=tools_internal::inspect_mutations(claim.workdir(),claim.revert_changeset_id());
    if(record["dialog_id"]!=claim.dialog_id())throw std::runtime_error("changeset belongs to another dialog");
    if(claim.revert_source_task_id()&&(record["task_id"]!=claim.revert_source_task_id()||record["turn_id"]!=claim.revert_source_turn_id()))throw std::runtime_error("changeset does not match source task");
    std::unique_ptr<WorkspaceLease> source_workspace;
    if(record.contains("workspace_identity"))source_workspace=std::make_unique<WorkspaceLease>(claim.workdir(),record["workspace_identity"].dump());
    const std::string command="revert_changeset "+claim.revert_changeset_id();
    const auto token=ConfirmHub::instance().begin(claim.dialog_id(),command);
    auto confirm=event();auto* c=confirm.mutable_confirm_needed();c->set_confirm_token(token);c->set_command(command);
    std::map<std::string,bool> paths;for(const auto& e:record["entries"])if(e["phase"]!="failed")paths.try_emplace(e["path"].get<std::string>(),e["before_exists"].get<bool>());
    std::string preview="Восстановит исходные файлы и удалит файлы, созданные этим ходом. Чужие изменения останутся с сообщением о конфликте.\n";
    size_t shown=0;for(const auto& [path,exists]:paths){if(shown==20||preview.size()>4096)break;preview+=(exists?"Восстановить: ":"Удалить созданный файл: ")+nlohmann::json(path).dump()+"\n";++shown;}
    if(shown<paths.size())preview+="Ещё файлов: "+std::to_string(paths.size()-shown)+". Полный список — в шагах исходного хода.";
    c->set_consequence(redact::text(preview));
    if(!sink(confirm)){ConfirmHub::instance().reject_dialog(claim.dialog_id());throw std::runtime_error("confirmation delivery failed");}
    if(!ConfirmHub::instance().wait_decision(claim.dialog_id(),token,cancel))throw std::runtime_error(stopped()?"отменено":"откат отклонён");
    if(stopped())throw std::runtime_error("отменено");
    const auto result=tools_internal::revert_mutations(claim.workdir(),claim.revert_changeset_id(),claim.dialog_id(),claim.turn_id(),claim.task_id(),[&]{if(check_workspace)check_workspace();if(source_workspace)source_workspace->check();return stopped();});
    changeset=result.value("journal","");detail=redact::text(result.dump());outcome=result["complete"].get<bool>()?"done":stopped()?"cancelled":"failed";
    auto observation=event();auto* o=observation.mutable_observation();o->set_tool("revert_changeset");o->set_ok(outcome=="done");o->set_output(detail);sink(observation);
  }catch(const std::exception& e){detail=redact::text(e.what());if(stopped())outcome="cancelled";}
  auto message=db.add_message(claim.dialog_id(),"assistant",detail,claim.turn_id());
  auto done=event();auto* d=done.mutable_done();d->set_message_id(message.id);d->set_changeset_id(changeset);d->set_assistant_text(detail);d->set_outcome(outcome);if(outcome!="done")d->set_error_code("revert_incomplete");sink(done);
}
}
