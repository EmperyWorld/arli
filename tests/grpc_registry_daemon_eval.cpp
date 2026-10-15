// Actual daemon/worker/Session/approval path; --mock is the deterministic CI mode.
#include "lifecycle_harness.hpp"
#include <google/protobuf/descriptor.pb.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
using namespace lt;
namespace fs=std::filesystem;
class App final:public arli::v1::Agent::Service {
 public:
  std::atomic<int> writes{0};
  grpc::Status CreateDialog(grpc::ServerContext*,const arli::v1::CreateDialogRequest* request,arli::v1::Dialog* reply)override {
    if(request->title()!="ready"&&request->title()!="model request")return {grpc::StatusCode::INVALID_ARGUMENT,"fixture value"};
    ++writes;reply->set_id(1);reply->set_title(request->title());return grpc::Status::OK;
  }
};
int main(int argc,char** argv) {
  if(argc!=6)return 2;
  const bool mocked=std::string(argv[3])=="--mock";
  Proc mock;std::string llm=argv[3],model=argv[4];
  if(mocked){
    const auto port=free_port();mock.start(argv[4],{std::to_string(port)},{});llm="http://127.0.0.1:"+std::to_string(port)+"/v1";model="grpc_provider_write";
    bool ready=false;const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(std::chrono::steady_clock::now()<until&&!ready){
      const int fd=socket(AF_INET,SOCK_STREAM,0);sockaddr_in addr{};addr.sin_family=AF_INET;
      addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port);
      ready=fd>=0&&connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0;
      if(fd>=0)close(fd);if(!ready)sleep_ms(50);
    }
    if(!ready){mock.kill9();return 3;}
  }
  char tmp[]="/tmp/arli-registry-daemon-XXXXXX";if(!mkdtemp(tmp))return 2;
  const fs::path root(tmp),wd=root/"workspace",dir=root/"grpc-tools";fs::create_directories(wd);
  const auto ep="127.0.0.1:"+std::to_string(free_port());
  Proc daemon;daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});
  Client client(ep);if(!client.wait_health(8000)){daemon.kill9();mock.kill9();return 3;}
  client.token=arli::secrets::load(root/"ui_token");
  if(!client.wait_worker("idle",8000)||!client.set_model(llm,model,wd.string(),180,8)){daemon.kill9();mock.kill9();return 3;}
  // Publish after worker startup: loading happens before each claim, never from model args.
  App app;grpc::ServerBuilder builder;int port=0;const std::string token="registry-fixture-service-token";
  builder.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials(token,"fixture-internal"),&port);
  builder.RegisterService(&app);auto server=builder.BuildAndStart();if(!server){daemon.kill9();mock.kill9();return 3;}
  fs::create_directory(dir);chmod(dir.c_str(),0700);
  const auto save=[&](const std::string& name,const std::string& bytes){std::ofstream(dir/name,std::ios::binary)<<bytes;chmod((dir/name).c_str(),0600);};
  google::protobuf::FileDescriptorSet fds;arli::v1::Dialog::descriptor()->file()->CopyTo(fds.add_file());
  save("app.pb",fds.SerializeAsString());save("app.token",token);
  nlohmann::json config={{"version",1},{"methods",nlohmann::json::array({{{"tool_name","grpc_write"},{"endpoint","127.0.0.1:"+std::to_string(port)},
    {"method","arli.v1.Agent.CreateDialog"},{"descriptor_file","app.pb"},{"token_file","app.token"},{"mutating",true},{"deadline_ms",1000}}})}};
  save("registry.json",config.dump());
  int approved=0,denied=0,observed=0;bool secret_leak=false;
  auto turn=[&](bool accept,bool expect_bad_config=false) {
    const auto dialog=client.create_dialog("registry integration");arli::v1::SendRequest req;req.set_dialog_id(dialog);
    req.set_text("Вызови grpc_write ровно один раз с аргументом title равным ready. Дождись подтверждения оператора. После ответа сообщи результат. Файловые инструменты и shell не используй.");
    grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(180));
    auto stream=client.stub->Send(&ctx,req);arli::v1::TurnEvent ev;std::string outcome;bool invalid=false;
    while(stream->Read(&ev)) {
      secret_leak|=ev.SerializeAsString().find(token)!=std::string::npos;
      if(ev.has_confirm_needed()) {
        const std::string expected=mocked?"grpc_write {\"title\":\"model request\"}":"grpc_write {\"title\":\"ready\"}";
        const bool allow=accept&&approved==0&&ev.confirm_needed().command()==expected;
        grpc::ClientContext control;client.auth(control);control.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
        arli::v1::ConfirmDangerRequest answer;answer.set_dialog_id(dialog);answer.set_confirm_token(ev.confirm_needed().confirm_token());answer.set_accept(allow);
        arli::v1::ConfirmDangerReply reply;
        if(client.stub->ConfirmDanger(&control,answer,&reply).ok()&&reply.ok()){if(allow)++approved;else ++denied;}
      }
      if(ev.has_observation()&&ev.observation().tool()=="grpc_write"&&ev.observation().ok())++observed;
      if(ev.has_done())outcome=ev.done().outcome();
      if(ev.has_error())invalid|=ev.error().message().find("GRPC_REGISTRY_INVALID")!=std::string::npos;
      if(ev.has_ask_user())ctx.TryCancel();
    }
    const bool transport=stream->Finish().ok();
    return expect_bad_config?invalid:(transport&&outcome==(accept?"done":"failed"));
  };
  bool passed=turn(true)&&approved==1&&observed==1&&app.writes==1;
  if(mocked){passed=turn(false)&&passed&&denied>0&&app.writes==1;save("registry.json","{bad");passed=turn(false,true)&&passed&&app.writes==1;}
  passed&=!secret_leak;
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();mock.term();if(mock.pid&&mock.wait(2000)<0)mock.kill9();server->Shutdown();server->Wait();
  nlohmann::json report={{"passed",passed},{"model",model},{"mocked",mocked},{"through_daemon",true},{"approved",approved},{"denied",denied},
    {"service_writes",app.writes.load()},{"rpc_observations",observed},{"secret_leak",secret_leak},{"workspace",root.string()}};
  std::ofstream(argv[5])<<report.dump(2)<<'\n';return passed?0:1;
}
