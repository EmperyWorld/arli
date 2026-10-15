#include "lifecycle_harness.hpp"
#include <filesystem>
#include <iostream>
using namespace lt;
namespace fs=std::filesystem;
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  char tmp[]="/tmp/arli-job-owner-daemon-XXXXXX";if(!mkdtemp(tmp))return 2;
  const fs::path wd=fs::path(tmp)/"workspace";fs::create_directories(wd);
  const auto port=free_port();Proc mock;mock.start(argv[3],{std::to_string(port)},{});sleep_ms(100);
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env={{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon;daemon.start(argv[1],{ep},env);Client client(ep);
  if(!client.wait_health(8000)){daemon.kill9();mock.kill9();return 3;}
  client.token=arli::secrets::load(fs::path(tmp)/"ui_token");
  LT_CHECK(client.wait_worker("idle",8000)&&client.set_model("http://127.0.0.1:"+std::to_string(port)+"/v1","job_owner",wd.string(),30,8),"worker configured");
  const auto dialog=client.create_dialog("job owner"),other=client.create_dialog("other");
  arli::v1::SendRequest request;request.set_dialog_id(dialog);request.set_text("Запусти в фоне sleep 30 и дождись подтверждения оператора.");
  grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(15));
  auto stream=client.stub->Send(&ctx,request);arli::v1::TurnEvent event;std::int64_t task=0,turn=0;bool done=false;
  while(stream->Read(&event)){
    if(event.task_id())task=event.task_id();if(event.turn_id())turn=event.turn_id();
    if(event.has_confirm_needed()){
      grpc::ClientContext control;client.auth(control);control.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
      arli::v1::ConfirmDangerRequest answer;answer.set_dialog_id(dialog);answer.set_confirm_token(event.confirm_needed().confirm_token());answer.set_accept(event.confirm_needed().command()=="sleep 30");
      arli::v1::ConfirmDangerReply reply;LT_CHECK(client.stub->ConfirmDanger(&control,answer,&reply).ok()&&reply.ok(),"approval relay");
    }
    if(event.has_done())done=event.done().outcome()=="done";
  }
  LT_CHECK(stream->Finish().ok()&&done&&task>0&&turn>0,"real task completed after launching background process");
  auto list=[&](std::int64_t d,std::int64_t t){grpc::ClientContext query;client.auth(query);query.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
    arli::v1::ListJobsRequest req;req.set_include_finished(true);req.set_dialog_id(d);req.set_task_id(t);arli::v1::ListJobsReply reply;
    LT_CHECK(client.stub->ListJobs(&query,req,&reply).ok(),"ListJobs RPC");return reply;};
  auto jobs=list(dialog,task);LT_CHECK(jobs.jobs_size()==1,"scoped list");
  const auto id=jobs.jobs_size()?jobs.jobs(0).id():0;
  if(jobs.jobs_size())LT_CHECK(jobs.jobs(0).dialog_id()==dialog&&jobs.jobs(0).task_id()==task&&jobs.jobs(0).turn_id()==turn,"owner originates from Claim");
  LT_CHECK(list(other,0).jobs_size()==0&&list(0,0).jobs_size()==1,"other dialog excluded and legacy global list preserved");
  auto stop=[&](std::int64_t expected){grpc::ClientContext control;client.auth(control);control.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
    arli::v1::StopJobRequest req;req.set_id(id);req.set_expected_dialog_id(expected);req.set_expected_task_id(task);arli::v1::StopJobReply reply;
    LT_CHECK(client.stub->StopJob(&control,req,&reply).ok(),"StopJob RPC");return reply.ok();};
  LT_CHECK(!stop(other),"wrong owner cannot stop selected process");jobs=list(dialog,task);
  LT_CHECK(jobs.jobs_size()==1&&jobs.jobs(0).status()=="running","mismatch sends no signal");
  LT_CHECK(stop(dialog),"matching owner stops job");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();daemon.start(argv[1],{ep},env);
  LT_CHECK(client.wait_health(8000),"daemon restart");jobs=list(dialog,task);
  LT_CHECK(jobs.jobs_size()==1&&jobs.jobs(0).task_id()==task&&jobs.jobs(0).status()=="stopped","ownership and terminal state survive restart");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();mock.term();if(mock.wait(2000)<0)mock.kill9();
  std::cout<<"JOB_OWNERSHIP_DAEMON_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
