#include "lifecycle_harness.hpp"
#include "db.hpp"
#include <nlohmann/json.hpp>
#include <future>
#include <fstream>
using namespace lt;namespace fs=std::filesystem;using json=nlohmann::json;
int main(int argc,char** argv){
  if(argc!=4)return 2;char tmp[]="/tmp/arli-workspace-queue-XXXXXX";if(!mkdtemp(tmp))return 2;const auto root=fs::path(tmp),wd=root/"workspace",block=wd/"block",a=wd/"A",b=wd/"B",alias=wd/"alias";
  for(const auto& dir:{block,a,b})fs::create_directories(dir);fs::create_directory_symlink(a,alias);
  const auto port=free_port();const std::vector<std::string> mock_args{std::to_string(port)};Proc mock;mock.start(argv[3],mock_args,{});const auto url="http://127.0.0.1:"+std::to_string(port)+"/v1",ep="127.0.0.1:"+std::to_string(free_port());
  Proc daemon;daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});Client client(ep);LT_CHECK(client.wait_health(8000),"health");client.token=arli::secrets::load(root/"ui_token");LT_CHECK(client.wait_worker("idle",8000),"worker");
  auto wait_status=[&](int64_t d,const std::string& status){for(int i=0;i<100;++i){if(client.last_task(d).status()==status)return true;sleep_ms(10);}return false;};
  for(int scenario=0;scenario<2;++scenario){
    LT_CHECK(client.set_model(url,"slow:3000",block.string(),30,3),"blocking model configured");const auto first=client.create_dialog("block"),second=client.create_dialog("queued");
    auto running=std::async(std::launch::async,[&]{return client.send(first,"first",15000);});LT_CHECK(wait_status(first,"running"),"first occupies worker");
    LT_CHECK(client.set_model(url,"echo",(scenario==0?alias:a).string(),30,3),"target workspace configured");auto queued=std::async(std::launch::async,[&]{return client.send(second,"second",15000);});LT_CHECK(wait_status(second,"queued"),"second is queued");
    const auto info=client.last_task(second);{arli::Database db((root/"arli.db").string());const auto task=db.get_task(info.id());LT_CHECK(task&&json::parse(task->config_json)["workdir"]==a.string(),"canonical workspace persisted before mutation");}
    if(scenario==0){fs::remove(alias);fs::create_directory_symlink(b,alias);}else{fs::rename(a,wd/"old-A");fs::create_directory(a);}
    auto r1=running.get(),r2=queued.get();LT_CHECK(r1.done(),"blocking task finished");
    if(scenario==0)LT_CHECK(r2.done()&&client.last_task(second).status()=="done","alias retarget does not redirect queued task");
    else LT_CHECK(client.last_task(second).status()=="failed"&&client.last_task(second).error().find("WORKSPACE_CHANGED")!=std::string::npos,"worker rejects replaced root before LLM/tools");
  }
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();mock.term();if(mock.wait(3000)<0)mock.kill9();std::cout<<"WORKSPACE_QUEUE_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
