#include "lifecycle_harness.hpp"
#include "tools/mutation_journal.hpp"
#include "tools/atomic_publish.hpp"
#include <fstream>
using namespace lt;namespace fs=std::filesystem;
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  if(argc!=3)return 2;char tmp[]="/tmp/arli-revert-daemon-XXXXXX";if(!mkdtemp(tmp))return 2;
  setenv("ARLI_STATE_DIR",tmp,1);const auto wd=fs::path(tmp)/"workspace";fs::create_directories(wd);
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);LT_CHECK(client.wait_health(8000),"health");client.token=arli::secrets::load(fs::path(tmp)/"ui_token");
  LT_CHECK(client.wait_worker("idle",8000)&&client.set_model("http://127.0.0.1:1/v1","unavailable",wd.string(),30,3),"worker configured without reachable model");
  auto dialog=client.create_dialog("revert"),other=client.create_dialog("other");
  std::ofstream(wd/"original.txt")<<"before";std::string id;
  {arli::tools_internal::MutationScope scope(wd,dialog,10);LT_CHECK(arli::publish_file(wd/"original.txt","after",true).ok&&arli::publish_file(wd/"created.txt","created",false).ok,"fixture publications");id=scope.path().parent_path().filename().string();}
  int confirmations=0;
  auto run=[&](int64_t owner,const std::string& request,int decision,bool auth=true){
    Collected out;grpc::ClientContext ctx;if(auth)client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(15));
    arli::v1::RevertChangesetRequest req;req.set_dialog_id(owner);req.set_changeset_id(id);req.set_request_id(request);
    auto stream=client.stub->RevertChangeset(&ctx,req);arli::v1::TurnEvent event;
    while(stream->Read(&event)){out.events.push_back(event);
      if(event.has_confirm_needed()&&event.interaction_active()){
        ++confirmations;LT_CHECK(read(wd/"original.txt")=="after"&&fs::exists(wd/"created.txt"),"approval precedes mutations");
        grpc::ClientContext control;client.auth(control);control.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        if(decision<0){arli::v1::CancelRequest cancel;cancel.set_dialog_id(owner);arli::v1::CancelReply reply;LT_CHECK(client.stub->Cancel(&control,cancel,&reply).ok(),"cancel pending revert");}
        else{arli::v1::ConfirmDangerRequest answer;answer.set_dialog_id(owner);answer.set_confirm_token(event.confirm_needed().confirm_token());answer.set_accept(decision>0);arli::v1::ConfirmDangerReply reply;LT_CHECK(client.stub->ConfirmDanger(&control,answer,&reply).ok()&&reply.ok(),"approval delivery");}
      }
    }out.status=stream->Finish();return out;
  };
  LT_CHECK(run(dialog,"unauth",0,false).status.error_code()==grpc::StatusCode::UNAUTHENTICATED,"auth enforced");
  auto wrong=run(other,"owner",1);LT_CHECK(wrong.has([](const auto& e){return e.has_done()&&e.done().outcome()=="failed";})&&confirmations==0,"wrong owner rejected before approval");
  auto readonly=[&](bool value){grpc::ClientContext get;client.auth(get);get.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));arli::v1::Settings settings;LT_CHECK(client.stub->GetSettings(&get,arli::v1::GetSettingsRequest(),&settings).ok(),"read settings");settings.set_read_only(value);grpc::ClientContext set;client.auth(set);set.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));arli::v1::Settings reply;LT_CHECK(client.stub->SetSettings(&set,settings,&reply).ok(),"set read-only");};
  readonly(true);auto ro=run(dialog,"readonly",1);LT_CHECK(ro.has([](const auto& e){return e.has_done()&&e.done().outcome()=="failed";})&&confirmations==0&&read(wd/"original.txt")=="after","read-only denies undo before approval");readonly(false);
  auto saved_id=id;id="../invalid";LT_CHECK(run(dialog,"invalid",1).status.error_code()==grpc::StatusCode::INVALID_ARGUMENT,"invalid changeset ID rejected");id=saved_id;
  auto denied=run(dialog,"denied",0);LT_CHECK(denied.status.ok()&&denied.has([](const auto& e){return e.has_done()&&e.done().outcome()=="failed";})&&read(wd/"original.txt")=="after","denial preserves files");
  auto cancelled=run(dialog,"cancel",-1);LT_CHECK(cancelled.status.ok()&&read(wd/"original.txt")=="after"&&fs::exists(wd/"created.txt"),"cancel preserves files");
  auto accepted=run(dialog,"accepted",1);LT_CHECK(accepted.status.ok()&&accepted.has([](const auto& e){return e.has_done()&&e.done().outcome()=="done";})&&read(wd/"original.txt")=="before"&&!fs::exists(wd/"created.txt"),"approved worker revert without LLM");
  const int approvals=confirmations;auto replay=run(dialog,"accepted",1);LT_CHECK(replay.status.ok()&&confirmations==approvals,"request dedup replays without approval or mutation");
  grpc::ClientContext collision;client.auth(collision);collision.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));arli::v1::SendRequest send;send.set_dialog_id(dialog);send.set_text("Откат изменений "+id);send.set_request_id("accepted");auto stream=client.stub->Send(&collision,send);arli::v1::TurnEvent ignored;while(stream->Read(&ignored)){}LT_CHECK(stream->Finish().error_code()==grpc::StatusCode::ALREADY_EXISTS,"Send cannot alias revert request");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);client.reconnect(ep);LT_CHECK(client.wait_health(8000),"restart");replay=run(dialog,"accepted",1);LT_CHECK(replay.status.ok()&&confirmations==approvals&&read(wd/"original.txt")=="before","durable operation dedup after restart");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::cout<<"REVERT_DAEMON_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
