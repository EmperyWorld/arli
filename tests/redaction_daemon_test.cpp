#include "lifecycle_harness.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace lt;
namespace fs=std::filesystem;
int main(int argc,char** argv) {
  if(argc!=4)return 2;
  char tmp[]="/tmp/arli-redaction-daemon-XXXXXX";if(!mkdtemp(tmp))return 2;
  const fs::path root(tmp),wd=root/"workspace";fs::create_directories(wd);
  const auto port=free_port();Proc mock;mock.start(argv[3],{std::to_string(port)},{});sleep_ms(100);
  const auto ep="127.0.0.1:"+std::to_string(free_port());Proc daemon;
  daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});
  Client client(ep);if(!client.wait_health(8000)){daemon.kill9();mock.kill9();return 3;}
  client.token=arli::secrets::load(root/"ui_token");const auto internal=arli::secrets::load(root/"internal_token");
  const std::string key="fixture-llm-key-1234567890",next_key="fixture-rotated-key-987654321";
  LT_CHECK(client.wait_worker("idle",8000),"worker ready");
  arli::v1::Settings settings;settings.set_llm_base_url("http://127.0.0.1:"+std::to_string(port)+"/v1");
  settings.set_llm_model("echo");settings.set_workdir(wd.string());settings.set_llm_api_key(key);settings.set_max_steps(8);
  auto save=[&]{grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
    arli::v1::Settings reply;return client.stub->SetSettings(&ctx,settings,&reply).ok()&&reply.llm_api_key().empty();};
  LT_CHECK(save(),"model key stays write-only");
  const auto title="credentials "+key+" "+client.token+" "+internal;
  const auto dialog=client.create_dialog(title);
  arli::v1::SendRequest request;request.set_dialog_id(dialog);request.set_request_id("privacy-retry");request.set_text("Ответь текстом: "+title);
  grpc::ClientContext ctx;client.auth(ctx);ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(20));
  auto reader=client.stub->Send(&ctx,request);arli::v1::TurnEvent event;std::string wire;bool done=false;
  while(reader->Read(&event)){wire+=event.SerializeAsString();done|=event.has_done()&&event.done().outcome()=="done";}
  LT_CHECK(reader->Finish().ok()&&done,"real worker completes echo turn");
  auto clean=[&](const std::string& bytes){return bytes.find(key)==std::string::npos&&bytes.find(client.token)==std::string::npos&&bytes.find(internal)==std::string::npos;};
  LT_CHECK(clean(wire)&&wire.find("[redacted]")!=std::string::npos,"stream and Done do not expose credentials");
  settings.set_llm_api_key(next_key);LT_CHECK(save(),"credential rotation");
  grpc::ClientContext open_ctx;client.auth(open_ctx);open_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
  arli::v1::OpenDialogRequest open;open.set_dialog_id(dialog);arli::v1::OpenDialogReply history;
  LT_CHECK(client.stub->OpenDialog(&open_ctx,open,&history).ok()&&clean(history.SerializeAsString()),"history/title/trace protect previous credential after rotation");
  grpc::ClientContext list_ctx;client.auth(list_ctx);list_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
  arli::v1::ListTasksRequest list;list.set_dialog_id(dialog);arli::v1::ListTasksReply tasks;
  LT_CHECK(client.stub->ListTasks(&list_ctx,list,&tasks).ok()&&clean(tasks.SerializeAsString()),"task text is redacted in UI");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();
  daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});
  LT_CHECK(client.wait_health(8000)&&client.wait_worker("idle",8000),"daemon and worker restarted");
  grpc::ClientContext restored_ctx;client.auth(restored_ctx);restored_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
  history.Clear();LT_CHECK(client.stub->OpenDialog(&restored_ctx,open,&history).ok()&&clean(history.SerializeAsString()),"old credential in raw dialog title stays hidden after rotation and restart");
  grpc::ClientContext retry_ctx;client.auth(retry_ctx);retry_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
  auto replay=client.stub->Send(&retry_ctx,request);std::string replayed;while(replay->Read(&event))replayed+=event.SerializeAsString();
  LT_CHECK(replay->Finish().ok()&&clean(replayed),"original request retry still matches and replays safely after restart");
  std::ofstream(wd/"README.md")<<"private fixture: "<<key;
  settings.set_llm_model("tool_read_secret");LT_CHECK(save(),"tool read scenario");
  arli::v1::SendRequest read_request;read_request.set_dialog_id(client.create_dialog("tool privacy"));read_request.set_text("Прочитай README.md");
  grpc::ClientContext read_ctx;client.auth(read_ctx);read_ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(10));
  auto reading=client.stub->Send(&read_ctx,read_request);std::string read_events;bool model_clean=false;
  while(reading->Read(&event)){read_events+=event.SerializeAsString();if(event.has_done())model_clean=event.done().assistant_text().find("tool content clean")!=std::string::npos;}
  LT_CHECK(reading->Finish().ok()&&model_clean&&clean(read_events),"tool output is sanitized before model, observation and storage after restart");
  std::ifstream ledger(root/"redaction.fingerprints");const std::string fingerprints{std::istreambuf_iterator<char>(ledger),{}};
  LT_CHECK(clean(fingerprints)&&fingerprints.find(next_key)==std::string::npos,"ledger stores fingerprints, not credentials");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();mock.term();if(mock.wait(2000)<0)mock.kill9();
  for(const auto& entry:fs::recursive_directory_iterator(root)) {
    const auto path=entry.path();if(!entry.is_regular_file()||!(path.extension()==".log"||path.extension()==".json"||path.extension()==".jsonl"))continue;
    std::ifstream f(path);const std::string bytes{std::istreambuf_iterator<char>(f),{}};
    LT_CHECK(clean(bytes),"logs/audit contain no known credential");
  }
  std::cout<<"REDACTION_DAEMON_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
