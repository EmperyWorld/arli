// External SDK acceptance: no Arli source-tree helpers, core library or database API.
#include <arli/auth.hpp>
#include <arli/secrets.hpp>
#include <arli/arli.grpc.pb.h>
#include <filesystem>
#include <thread>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
namespace fs = std::filesystem;
using namespace std::chrono_literals;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int free_port() {
  int fd=socket(AF_INET,SOCK_STREAM,0); require(fd>=0,"socket");
  sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  require(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"bind");
  socklen_t size=sizeof(address); require(getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size)==0,"port");
  close(fd); return ntohs(address.sin_port);
}
struct Process {
  pid_t pid=0;
  void start(const std::string& binary,const std::string& argument,const fs::path& log) {
    pid=fork(); require(pid>=0,"fork");
    if(pid==0) {
      setsid(); int fd=open(log.c_str(),O_CREAT|O_WRONLY|O_APPEND,0600);
      if(fd>=0){dup2(fd,1);dup2(fd,2);close(fd);}
      execl(binary.c_str(),binary.c_str(),argument.c_str(),nullptr);_exit(127);
    }
  }
  ~Process() {
    if(pid<=0)return;
    kill(pid,SIGTERM); int status=0;
    for(int n=0;n<100;++n){if(waitpid(pid,&status,WNOHANG)==pid)return;std::this_thread::sleep_for(20ms);}
    kill(-pid,SIGKILL);waitpid(pid,&status,0);
  }
};
struct Client {
  std::unique_ptr<arli::v1::Agent::Stub> stub;
  std::string token;
  void context(grpc::ClientContext& ctx,int seconds=5) {
    arli::auth::bearer(ctx,token);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(seconds));
  }
  void model(const std::string& url,const fs::path& wd,const std::string& name) {
    grpc::ClientContext ctx;context(ctx);arli::v1::Settings request,reply;
    request.set_llm_base_url(url);request.set_llm_model(name);request.set_workdir(wd.string());
    request.set_idle_timeout_sec(60);request.set_cancel_grace_sec(2);
    require(stub->SetSettings(&ctx,request,&reply).ok(),"configure isolated mock model");
  }
  arli::v1::ListTasksReply tasks(int64_t dialog) {
    grpc::ClientContext ctx;context(ctx);arli::v1::ListTasksRequest req;req.set_dialog_id(dialog);
    arli::v1::ListTasksReply reply;require(stub->ListTasks(&ctx,req,&reply).ok(),"list tasks");return reply;
  }
};
int main(int argc,char** argv) {
  if(argc!=5)return 2;
  try {
    const fs::path state=argv[4],wd=state/"workspace";fs::create_directories(wd);
    const auto endpoint="127.0.0.1:"+std::to_string(free_port());
    auto model_port=std::to_string(free_port());
    while(endpoint=="127.0.0.1:"+model_port)model_port=std::to_string(free_port());
    const auto url="http://127.0.0.1:"+model_port+"/v1";
    setenv("ARLI_STATE_DIR",state.c_str(),1);setenv("ARLI_AGENT_BIN",argv[2],1);setenv("ARLI_LISTEN",endpoint.c_str(),1);
    unsetenv("ARLI_UI_TOKEN");unsetenv("ARLI_INTERNAL_TOKEN");unsetenv("ARLI_WORKER_LAUNCH_TOKEN");
    Process mock,daemon;mock.start(argv[3],model_port,state/"process.log");daemon.start(argv[1],endpoint,state/"process.log");
    Client client;client.stub=arli::v1::Agent::NewStub(arli::auth::make_channel(endpoint));
    bool ready=false; std::string last_health;
    for(int n=0;n<100&&!ready;++n) {
      grpc::ClientContext ctx;client.context(ctx,1);ctx.set_wait_for_ready(true);arli::v1::HealthReply reply;
      const auto status=client.stub->Health(&ctx,arli::v1::HealthRequest{},&reply);
      last_health=std::to_string(status.error_code())+" "+status.error_message()+" "+reply.ShortDebugString();
      ready=status.ok()&&reply.serving()&&reply.worker_state()=="idle"&&reply.event_resume_supported();
      if(!ready)std::this_thread::sleep_for(50ms);
    }
    if(!ready)std::cerr<<"Health diagnostic: "<<last_health<<'\n';
    require(ready,"production Health and worker ready");
    client.token=arli::secrets::load(state/"ui_token");require(!client.token.empty(),"UI credential available");
    client.model(url,wd,"slow:1000");
    grpc::ClientContext create;client.context(create);arli::v1::CreateDialogRequest cr;cr.set_title("SDK acceptance");arli::v1::Dialog dialog;
    require(client.stub->CreateDialog(&create,cr,&dialog).ok(),"create dialog");
    arli::v1::SendRequest request;request.set_dialog_id(dialog.id());request.set_request_id("external-sdk-once");request.set_text("Привет из установленного SDK");
    int64_t task=0,cursor=0;
    {
      grpc::ClientContext ctx;client.context(ctx);auto reader=client.stub->Send(&ctx,request);arli::v1::TurnEvent ev;
      require(reader->Read(&ev),"Send emits durable event");task=ev.task_id();cursor=ev.event_id();require(task>0&&cursor>0,"task and cursor provided");
      ctx.TryCancel();reader->Finish();
    }
    {
      grpc::ClientContext ctx;client.context(ctx,10);arli::v1::AttachTurnRequest attach;
      attach.set_dialog_id(dialog.id());attach.set_task_id(task);attach.set_after_event_id(cursor);
      auto reader=client.stub->AttachTurn(&ctx,attach);arli::v1::TurnEvent ev;bool done=false;int count=0;
      while(reader->Read(&ev)) {
        require(ev.task_id()==task&&ev.event_id()>cursor,"resume preserves task and strictly increasing cursor");cursor=ev.event_id();++count;
        if(ev.has_done())done=ev.done().outcome()=="done"&&ev.done().assistant_text().find("SDK")!=std::string::npos;
      }
      require(reader->Finish().ok()&&done&&count>0,"AttachTurn completes original task");
    }
    {
      grpc::ClientContext ctx;client.context(ctx);auto reader=client.stub->Send(&ctx,request);arli::v1::TurnEvent ev;bool done=false;
      while(reader->Read(&ev)){require(ev.task_id()==task,"idempotent Send uses original task");done|=ev.has_done();}
      require(reader->Finish().ok()&&done&&client.tasks(dialog.id()).tasks_size()==1,"retry never enqueues duplicate");
    }
    client.model(url,wd,"slow:5000");request.set_request_id("external-sdk-cancel");request.set_text("Привет, второй ход");
    grpc::ClientContext stream_ctx;client.context(stream_ctx,15);auto reader=client.stub->Send(&stream_ctx,request);arli::v1::TurnEvent ev;
    require(reader->Read(&ev),"second Send starts");const auto cancelled_task=ev.task_id();
    bool running=false;
    for(int n=0;n<100&&!running;++n){const auto rows=client.tasks(dialog.id());for(const auto& row:rows.tasks())if(row.id()==cancelled_task)running=row.status()=="running";if(!running)std::this_thread::sleep_for(20ms);}
    require(running,"second task is running before Cancel");
    grpc::ClientContext cancel_ctx;client.context(cancel_ctx);arli::v1::CancelRequest cancel;cancel.set_dialog_id(dialog.id());arli::v1::CancelReply cancelled;
    require(client.stub->Cancel(&cancel_ctx,cancel,&cancelled).ok()&&cancelled.ok(),"Cancel accepted by daemon");
    bool terminal=false;
    while(reader->Read(&ev))if(ev.has_done())terminal=ev.done().outcome()=="cancelled";
    require(reader->Finish().ok()&&terminal,"stream reports cancelled outcome");
    bool persisted=false;const auto final_rows=client.tasks(dialog.id());for(const auto& row:final_rows.tasks())if(row.id()==cancelled_task)persisted=row.status()=="cancelled";
    require(persisted,"cancelled status persisted");
    std::cout<<"SDK_DAEMON_ROUNDTRIP_OK health send resume retry cancel\n";return 0;
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
