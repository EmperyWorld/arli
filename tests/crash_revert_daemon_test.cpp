#include "lifecycle_harness.hpp"
#include "db.hpp"
#include "mutation_catalog.hpp"
#include "tools/mutation_journal.hpp"
#include "tools/atomic_publish.hpp"
#include <fstream>
#include <sys/wait.h>
using namespace lt;namespace fs=std::filesystem;using json=nlohmann::json;
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
  if(argc!=3)return 2;char tmp[]="/tmp/arli-crash-revert-XXXXXX";if(!mkdtemp(tmp))return 2;setenv("ARLI_STATE_DIR",tmp,1);
  const auto wd=fs::path(tmp)/"workspace"/"original",other=fs::path(tmp)/"workspace"/"other";fs::create_directories(wd);fs::create_directories(other);std::ofstream(wd/"file.txt")<<"before";std::ofstream(other/"file.txt")<<"unrelated";
  int64_t dialog,task;
  {arli::Database db((fs::path(tmp)/"arli.db").string());db.init();auto settings=db.get_settings();settings.workdir=wd.string();db.set_settings(settings);dialog=db.create_dialog("crashed").id;db.add_message(dialog,"user","edit file",1);task=db.enqueue_task(dialog,1,"edit file","crashed").id;db.mark_running(task,"crashed-worker");}
  const json criteria={{"checks",json::array({{{"kind","file_equals"},{"path","file.txt"},{"expected","after"}}})},{"unverifiable",""}};
  const auto child=fork();if(child==0){arli::tools_internal::MutationScope scope(wd,dialog,1,task);scope.record_acceptance(criteria);if(!arli::publish_file(wd/"file.txt","after",true).ok)_exit(2);_exit(73);}int status=0;waitpid(child,&status,0);LT_CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==73,"actual process exits without Done/destructors");
  const auto ids=arli::task_changesets(wd,dialog,1,task);LT_CHECK(ids.size()==1,"durable task index survives child death");if(ids.empty())return 3;
  const auto ep="127.0.0.1:"+std::to_string(free_port());const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);LT_CHECK(client.wait_health(8000),"startup reconciliation");client.token=arli::secrets::load(fs::path(tmp)/"ui_token");LT_CHECK(client.wait_worker("idle",8000)&&client.set_model("http://127.0.0.1:1/v1","none",other.string(),30,3),"current workspace changed");
  grpc::ClientContext query;client.auth(query);query.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));arli::v1::GetTurnTraceRequest tr;tr.set_dialog_id(dialog);tr.set_turn_id(1);arli::v1::GetTurnTraceReply trace;
  LT_CHECK(client.stub->GetTurnTrace(&query,tr,&trace).ok(),"trace RPC");bool found=false,done=false;for(const auto& item:trace.items()){done|=item.kind()=="done";if(item.kind()=="changeset")found|=json::parse(item.args_json()).value("changesetId","")==ids[0];}LT_CHECK(found&&!done,"crash journal exposed without fabricated Done");
  bool criteria_found=false;for(const auto& item:trace.items())if(item.kind()=="acceptance")criteria_found|=json::parse(item.args_json())==criteria;LT_CHECK(criteria_found,"original criteria exposed after crash through authenticated trace RPC");
  grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(12));arli::v1::RevertChangesetRequest req;req.set_dialog_id(dialog);req.set_changeset_id(ids[0]);req.set_source_turn_id(1);req.set_request_id("recover");auto stream=client.stub->RevertChangeset(&ctx,req);arli::v1::TurnEvent ev;bool confirmed=false,finished=false;
  while(stream->Read(&ev)){if(ev.has_confirm_needed()&&ev.interaction_active()){confirmed=true;LT_CHECK(read(wd/"file.txt")=="after"&&read(other/"file.txt")=="unrelated","no mutation before approval");grpc::ClientContext c;client.auth(c);arli::v1::ConfirmDangerRequest answer;answer.set_dialog_id(dialog);answer.set_confirm_token(ev.confirm_needed().confirm_token());answer.set_accept(true);arli::v1::ConfirmDangerReply reply;LT_CHECK(client.stub->ConfirmDanger(&c,answer,&reply).ok()&&reply.ok(),"approve recovery");}if(ev.has_done())finished=ev.done().outcome()=="done";}
  LT_CHECK(stream->Finish().ok()&&confirmed&&finished&&read(wd/"file.txt")=="before"&&read(other/"file.txt")=="unrelated","original task workspace used after settings change");
  grpc::ClientContext mismatch;client.auth(mismatch);mismatch.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(10));req.set_source_turn_id(2);req.set_request_id("wrong-source");auto wrong=client.stub->RevertChangeset(&mismatch,req);bool rejected=false,prompted=false;
  while(wrong->Read(&ev)){prompted|=ev.has_confirm_needed();if(ev.has_done())rejected=ev.done().outcome()=="failed";}
  LT_CHECK(wrong->Finish().ok()&&rejected&&!prompted&&read(wd/"file.txt")=="before","changeset cannot be rebound to another source task");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();std::cout<<"CRASH_REVERT_DAEMON_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
