// Opt-in production run_turn + live model + isolated allowlisted application service.
#include "grpc_tool_provider.hpp"
#include "turn.hpp"
#include "confirm.hpp"
#include "auth.hpp"
#include <google/protobuf/descriptor.pb.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <unistd.h>
namespace fs=std::filesystem;
class App final:public arli::v1::Agent::Service {
 public:
  std::atomic<int> writes{0};std::string value;
  grpc::Status CreateDialog(grpc::ServerContext*,const arli::v1::CreateDialogRequest* request,arli::v1::Dialog* reply)override {
    value=request->title();++writes;reply->set_id(1);reply->set_title(value);return grpc::Status::OK;
  }
};
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  char root[]="/tmp/arli-live-grpc-tools-XXXXXX";if(!mkdtemp(root))return 2;
  setenv("ARLI_STATE_DIR",root,1);fs::create_directories(fs::path(root)/"workspace");
  const std::string token="live-fixture-provider-token-12345";
  App app;grpc::ServerBuilder builder;int port=0;
  builder.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials(token,"fixture-internal"),&port);
  builder.RegisterService(&app);auto server=builder.BuildAndStart();if(!server)return 2;
  google::protobuf::FileDescriptorSet descriptors;arli::v1::Dialog::descriptor()->file()->CopyTo(descriptors.add_file());
  arli::GrpcToolProvider provider;provider.add({"grpc_write","127.0.0.1:"+std::to_string(port),"arli.v1.Agent.CreateDialog",descriptors.SerializeAsString(),token,true,1000});
  arli::Database db(std::string(root)+"/test.db");db.init();const auto dialog=db.create_dialog("live grpc tools");
  arli::TurnConfig cfg;cfg.llm_base_url=argv[1];cfg.llm_model=argv[2];cfg.workdir=(fs::path(root)/"workspace").string();cfg.max_steps=8;
  std::atomic<bool> cancel{false};std::mutex mutex;std::condition_variable condition;bool finished=false;
  std::thread deadline([&]{std::unique_lock lock(mutex);if(!condition.wait_for(lock,std::chrono::minutes(3),[&]{return finished;}))cancel=true;});
  int approved=0,denied=0,observed=0;std::string outcome;
  arli::run_turn(db,cfg,dialog.id,"Вызови grpc_write ровно один раз с аргументом title равным ready. Это изменяющий RPC: дождись подтверждения оператора. После ответа сервиса сообщи результат. Файловые инструменты и shell не используй.",1,[&](const auto& event){
    if(event.has_confirm_needed()) {
      const auto& confirm=event.confirm_needed();
      const bool allow=approved==0&&confirm.command()=="grpc_write {\"title\":\"ready\"}";
      arli::ConfirmHub::instance().decide(dialog.id,confirm.confirm_token(),allow);if(allow)++approved;else ++denied;
    }
    if(event.has_observation()&&event.observation().tool()=="grpc_write"&&event.observation().ok())++observed;
    if(event.has_ask_user())cancel=true;
    if(event.has_done())outcome=event.done().outcome();
    return true;
  },{&cancel,&provider});
  {std::lock_guard lock(mutex);finished=true;}condition.notify_all();deadline.join();server->Shutdown();server->Wait();
  const bool passed=approved==1&&denied==0&&observed==1&&app.writes==1&&app.value=="ready"&&outcome=="done";
  const nlohmann::json report={{"passed",passed},{"model",argv[2]},{"approved_exact_fixture",approved},{"denied",denied},
    {"rpc_observations",observed},{"service_writes",app.writes.load()},{"service_value",app.value},{"outcome",outcome},
    {"through_daemon",false},{"workspace",root}};
  std::ofstream(argv[3])<<report.dump(2)<<'\n';return passed?0:1;
}
