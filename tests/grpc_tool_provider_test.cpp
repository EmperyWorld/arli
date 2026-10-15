#include "grpc_tool_provider.hpp"
#include "lifecycle_harness.hpp"
#include "turn.hpp"
#include "confirm.hpp"
#include <google/protobuf/descriptor.pb.h>
#include <nlohmann/json.hpp>
#include <filesystem>
using namespace lt;
using namespace std::chrono_literals;
const std::string credential="fixture-provider-credential-123456789";
class Service final:public arli::v1::Agent::Service {
 public:
  std::atomic<int> reads{0},writes{0};
  grpc::Status GetSettings(grpc::ServerContext*,const arli::v1::GetSettingsRequest*,arli::v1::Settings* reply)override {
    ++reads;reply->set_llm_model(credential);return grpc::Status::OK;
  }
  grpc::Status CreateDialog(grpc::ServerContext* ctx,const arli::v1::CreateDialogRequest* req,arli::v1::Dialog* reply)override {
    if(req->title()=="slow")for(int n=0;n<200;++n){if(ctx->IsCancelled())return {grpc::StatusCode::CANCELLED,"stopped"};sleep_ms(10);}
    ++writes;reply->set_id(writes);reply->set_title(req->title());return grpc::Status::OK;
  }
};
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  // Spawn mock before gRPC threads exist.
  Proc mock;const auto port=free_port();mock.start(argv[1],{std::to_string(port)},{});sleep_ms(100);
  Service service;grpc::ServerBuilder builder;int service_port=0;
  builder.AddListeningPort("127.0.0.1:0",arli::auth::server_credentials(credential,"fixture-internal"),&service_port);
  builder.RegisterService(&service);auto server=builder.BuildAndStart();if(!server)return 2;
  google::protobuf::FileDescriptorSet descriptors;
  arli::v1::HealthRequest::descriptor()->file()->CopyTo(descriptors.add_file());
  const auto endpoint="127.0.0.1:"+std::to_string(service_port);
  auto spec=[&](const std::string& name,const std::string& method,bool mutating=false,int timeout=1000) {
    return arli::GrpcToolMethod{name,endpoint,"arli.v1.Agent."+method,descriptors.SerializeAsString(),credential,mutating,timeout};
  };
  arli::GrpcToolProvider provider;provider.add(spec("grpc_read","GetSettings"));provider.add(spec("grpc_write","CreateDialog",true));
  provider.add(spec("grpc_timeout","CreateDialog",true,50));
  auto bad_token=spec("grpc_bad_token","GetSettings");bad_token.bearer_token="wrong-provider-credential";provider.add(bad_token);
  LT_CHECK(provider.schemas_json().find(credential)==std::string::npos,"schemas never contain service credential");
  auto result=provider.invoke("grpc_read","{}",true,nullptr);
  LT_CHECK(result.ok&&result.output.find(credential)==std::string::npos&&result.output.find("[redacted]")!=std::string::npos,"read RPC works and credential echo is redacted");
  const int reads=service.reads;
  LT_CHECK(!provider.invoke("grpc_forbidden","{}",false,nullptr).ok&&service.reads==reads,"nonallowlisted method is not invoked");
  LT_CHECK(!provider.invoke("grpc_read","{\"method\":\"/arli.v1.Agent/CreateDialog\"}",false,nullptr).ok&&service.reads==reads,"arguments cannot select another RPC");
  LT_CHECK(!provider.invoke("grpc_bad_token","{}",false,nullptr).ok&&service.reads==reads,"service-specific credentials enforced");
  int approvals=0;auto accept=[&](const std::string& text){++approvals;return text.find(credential)==std::string::npos;};
  LT_CHECK(!provider.invoke("grpc_write","{\"title\":42}",false,nullptr,accept).ok&&approvals==0&&service.writes==0,"schema mismatch rejected before approval and dispatch");
  LT_CHECK(!provider.invoke("grpc_write","{}",true,nullptr,accept).ok&&approvals==0,"read-only forbids mutation before approval");
  LT_CHECK(!provider.invoke("grpc_write","{}",false,nullptr,[](const auto&){return false;}).ok&&service.writes==0,"denied mutation never dispatched");
  LT_CHECK(provider.invoke("grpc_write","{\"title\":\"approved\"}",false,nullptr,accept).ok&&service.writes==1&&approvals==1,"approved mutation executed once");
  const auto started=std::chrono::steady_clock::now();
  result=provider.invoke("grpc_timeout","{\"title\":\"slow\"}",false,nullptr,accept);
  LT_CHECK(!result.ok&&result.output.find("неизвестен")!=std::string::npos&&std::chrono::steady_clock::now()-started<1s,"deadline bounded and mutation outcome marked uncertain");
  std::atomic<bool> cancel{false};std::thread interrupt([&]{sleep_ms(30);cancel=true;});
  result=provider.invoke("grpc_write","{\"title\":\"slow\"}",false,&cancel,accept);interrupt.join();
  LT_CHECK(!result.ok&&service.writes==1,"cancellation reaches in-flight RPC");
  for(const auto mode:{"endpoint","stream","duplicate","credential"}) {
    bool rejected=false;try{auto invalid=spec("grpc_invalid","GetSettings");
      if(std::string(mode)=="endpoint")invalid.endpoint="example.com:443";
      if(std::string(mode)=="stream")invalid.method="arli.v1.Agent.Send";
      if(std::string(mode)=="duplicate")invalid.tool_name="grpc_read";
      if(std::string(mode)=="credential")invalid.bearer_token="quoted\"credential";
      provider.add(invalid);
    }catch(const std::invalid_argument&){rejected=true;}
    LT_CHECK(rejected,"invalid registry configuration rejected");
  }
  char temp[]="/tmp/arli-grpc-provider-XXXXXX";if(!mkdtemp(temp))return 2;setenv("ARLI_STATE_DIR",temp,1);
  {
    arli::Database db(std::string(temp)+"/test.db");db.init();arli::TurnConfig cfg;
    cfg.workdir=std::string(temp)+"/workspace";cfg.llm_base_url="http://127.0.0.1:"+std::to_string(port)+"/v1";cfg.max_steps=8;
    for(const bool allowed:{false,true}) {
      cfg.llm_model="grpc_provider_write";const auto dialog=db.create_dialog("provider mutation");Collected events;int prompted=0;const int before=service.writes;
      arli::run_turn(db,cfg,dialog.id,"Вызови настроенный сервис",1,[&](const auto& ev){events.events.push_back(ev);
        if(ev.has_confirm_needed()){++prompted;arli::ConfirmHub::instance().decide(dialog.id,ev.confirm_needed().confirm_token(),allowed);}return true;},{nullptr,&provider});
      LT_CHECK(prompted==1&&service.writes==before+(allowed?1:0),"worker turn uses approval gate for external mutation");
      LT_CHECK(events.has([&](const auto& ev){return ev.has_done()&&ev.done().outcome()==(allowed?"done":"failed");}),"denied external action cannot become successful turn");
      LT_CHECK(events.has([](const auto& ev){return ev.has_action()&&ev.action().tool()=="grpc_write";})&&events.has([](const auto& ev){return ev.has_observation()&&ev.observation().tool()=="grpc_write";}),"external calls emit normal audit relay events");
    }
    cfg.llm_model="grpc_provider_forbidden";const auto dialog=db.create_dialog("forbidden");Collected events;const int before=service.writes;
    arli::run_turn(db,cfg,dialog.id,"Вызови сервис",1,[&](const auto& ev){events.events.push_back(ev);return true;},{nullptr,&provider});
    LT_CHECK(service.writes==before&&events.has([](const auto& ev){return ev.has_done()&&ev.done().outcome()=="failed";}),"worker refuses a model-selected forbidden method");
  }
  server->Shutdown();mock.kill9();std::filesystem::remove_all(temp);return g_failed?1:0;
}
