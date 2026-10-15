// Opt-in live model: only the literal harmless fixture command is approved.
#include "lifecycle_harness.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
using namespace lt;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  if (argc != 6) return 2;
  char tmp[] = "/tmp/arli-live-jobs-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), wd = root / "workspace"; fs::create_directories(wd);
  const auto ep = "127.0.0.1:" + std::to_string(free_port());
  const std::vector<std::pair<std::string,std::string>> env = {{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}};
  Proc daemon; daemon.start(argv[1], {ep}, env); Client client(ep);
  if (!client.wait_health(8000)) { daemon.kill9(); return 3; }
  client.token = arli::secrets::load(root / "ui_token");
  if (!client.wait_worker("idle",8000) || !client.set_model(argv[3],argv[4],wd.string(),180,5)) {
    daemon.term(); if (daemon.wait(8000)<0) daemon.kill9(); return 3;
  }
  const auto dialog = client.create_dialog("live background lifecycle");
  arli::v1::SendRequest req; req.set_dialog_id(dialog);
  req.set_text("Вызови start_background ровно один раз с command равным 'printf ready; exit 7'. Дождись подтверждения оператора. Файлы не меняй. После запуска сообщи только факт запуска, не объявляй успешное выполнение команды: код 7 ожидаем для проверки учёта ошибок.");
  grpc::ClientContext ctx; client.auth(ctx); ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(180));
  auto stream = client.stub->Send(&ctx,req); arli::v1::TurnEvent event;
  int approved=0, rejected=0, starts=0; bool done=false;std::int64_t task_id=0,turn_id=0;
  while (stream->Read(&event)) {
    if(event.task_id())task_id=event.task_id();if(event.turn_id())turn_id=event.turn_id();
    if (event.has_confirm_needed()) {
      grpc::ClientContext control; client.auth(control); control.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
      const bool allow=approved==0 && event.confirm_needed().command()=="printf ready; exit 7";
      arli::v1::ConfirmDangerRequest answer; answer.set_dialog_id(dialog);
      answer.set_confirm_token(event.confirm_needed().confirm_token()); answer.set_accept(allow);
      arli::v1::ConfirmDangerReply reply;
      if(client.stub->ConfirmDanger(&control,answer,&reply).ok() && reply.ok() && allow) ++approved; else ++rejected;
    }
    if(event.has_observation() && event.observation().tool()=="start_background" && event.observation().ok()) ++starts;
    if(event.has_done()) done=event.done().outcome()=="done";
    if(event.has_ask_user()) ctx.TryCancel();
  }
  const bool grpc_ok=stream->Finish().ok();
  daemon.term(); if(daemon.wait(8000)<0) daemon.kill9();
  daemon.start(argv[1],{ep},env);
  bool terminal=false,owner_verified=false; std::string status,log,finished_at; int exit_code=-1;std::int64_t job_id=0;
  if(client.wait_health(8000)) for(int attempt=0;attempt<100&&!terminal;++attempt) {
    grpc::ClientContext query; client.auth(query); query.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(2));
    arli::v1::ListJobsRequest list; list.set_include_finished(true); arli::v1::ListJobsReply reply;
    if(client.stub->ListJobs(&query,list,&reply).ok()&&reply.jobs_size()==1) {
      const auto& job=reply.jobs(0);job_id=job.id(); status=job.status();log=job.log_tail();exit_code=job.exit_code();finished_at=job.finished_at();
      owner_verified=job.dialog_id()==dialog&&job.task_id()==task_id&&job.turn_id()==turn_id&&task_id>0;
      terminal=!finished_at.empty()&&owner_verified&&status=="exited"&&job.exit_code_known()&&exit_code==7&&log=="ready";
    }
    if(!terminal)sleep_ms(20);
  }
  bool streamed=false;std::string stream_text;
  if(job_id){
    grpc::ClientContext log_ctx;client.auth(log_ctx);log_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
    arli::v1::WatchJobLogRequest watch;watch.set_id(job_id);watch.set_expected_dialog_id(dialog);watch.set_expected_task_id(task_id);
    auto logs=client.stub->WatchJobLog(&log_ctx,watch);arli::v1::JobLogChunk chunk;bool final=false,gap=false;
    while(logs->Read(&chunk)){stream_text+=chunk.text();final=chunk.terminal()&&chunk.exit_code_known()&&chunk.exit_code()==7;gap|=chunk.gap();}
    streamed=logs->Finish().ok()&&final&&!gap&&stream_text=="ready";
  }
  const bool passed=streamed&&grpc_ok&&done&&approved==1&&rejected==0&&starts==1&&terminal;
  nlohmann::json report={{"passed",passed},{"model",argv[4]},{"approved_exact_fixture",approved},{"rejected",rejected},
    {"streamed",streamed},{"stream_text",stream_text},{"starts",starts},{"grpc_ok",grpc_ok},{"done",done},{"after_daemon_restart",terminal},{"job_status",status},
    {"finished_at",finished_at},{"exit_code",exit_code},{"owner_verified",owner_verified},{"dialog_id",dialog},{"task_id",task_id},{"turn_id",turn_id},{"log",log},{"workspace",wd.string()}};
  std::ofstream(argv[5])<<report.dump(2)<<'\n';
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();return passed?0:1;
}
