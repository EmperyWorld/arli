#include "redact.hpp"
#include "redact_events.hpp"
#include "secrets.hpp"
#include "audit.hpp"
#include "log.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace fs=std::filesystem;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string read(const fs::path& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
int main(){try{
  char tmp[]="/tmp/arli-redaction-XXXXXX";require(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  const std::string secret="fixture-secret-0123456789",escaped="fixture-\"quoted\\secret";
  arli::redact::remember(secret);arli::redact::remember(escaped);
  require(arli::redact::text("before "+secret+" after")=="before [redacted] after","literal redaction");
  require(arli::redact::text(nlohmann::json(escaped).dump()).find("quoted")==std::string::npos,"JSON escape redaction");
  for(size_t split=0;split<=secret.size();++split) {
    arli::redact::Stream stream;
    auto output=stream.push("before "+secret.substr(0,split));output+=stream.push(secret.substr(split)+" after");output+=stream.finish();
    require(output=="before [redacted] after","all split boundaries");
  }
  arli::redact::Stream bytes;std::string joined;
  for(char c:secret)joined+=bytes.push(std::string(1,c));joined+=bytes.finish();
  require(joined=="[redacted]","single byte stream");
  arli::redact::Stream prefix;require(prefix.push("fixture-secret-").empty()&&prefix.finish()=="[redacted]","incomplete prefix never flushed");
  arli::redact::remember("b-prefix-fixture-credential");
  arli::redact::Stream ambiguous;auto tail=ambiguous.push(std::string(100,'b'));tail+=ambiguous.finish();
  require(tail==std::string(99,'b')+"[redacted]","unterminated one-byte credential prefix is conservatively masked");
  arli::redact::Stream terminated;auto line=terminated.push(std::string(100,'b')+"\n");line+=terminated.finish();
  require(line==std::string(100,'b')+"\n","terminated ordinary line is preserved");
  std::string events;auto sink=arli::redact_events([&](const auto& event){events+=event.SerializeAsString();return true;});
  for(char c:secret){arli::v1::TurnEvent event;event.mutable_token()->set_text(std::string(1,c));sink(event);}
  arli::v1::TurnEvent done;done.mutable_done()->set_assistant_text(secret);sink(done);
  require(events.find(secret)==std::string::npos&&events.find("[redacted]")!=std::string::npos,"event stream and terminal scrub");
  arli::log::info("test",secret);
  require(read(fs::path(tmp)/"arli.log").find(secret)==std::string::npos,"log file scrub");
  arli::TaskRow task;task.id=1;task.text=std::string(4090,'x')+secret;task.error=secret;
  arli::audit::open_task(task,secret,secret);
  arli::StepRow step;step.task_id=1;step.output=task.text;step.args_json=nlohmann::json{{"value",escaped}}.dump();
  arli::audit::append_event(1,step);arli::audit::close_task(task);
  for(const auto& file:{"task.json","events.jsonl"}) {
    auto output=read(fs::path(tmp)/"audit/1"/file);
    require(output.find("fixture-secret")==std::string::npos&&output.find("quoted")==std::string::npos,"audit redacts before truncation");
  }
  auto db=std::make_shared<arli::Database>((fs::path(tmp)/"test.db").string());db->init();
  auto settings=db->get_settings();settings.llm_api_key=secret;settings.llm_base_url="http://127.0.0.1:1/v1";db->set_settings(settings);
  const auto dialog=db->create_dialog(secret);db->add_message(dialog.id,"assistant",secret,1);
  require(db->list_messages(dialog.id)[0].text=="[redacted]","stored assistant message scrub");
  step.dialog_id=dialog.id;step.turn_id=1;step.kind="observation";step.output=secret;db->add_step(step);
  const auto queued=db->enqueue_task(dialog.id,1,"fixture");
  arli::v1::TurnEvent event;event.mutable_done()->set_assistant_text(secret);db->append_event(queued.id,event);
  require(db->events_after(queued.id,0)[0].done().assistant_text()=="[redacted]","durable event scrub");
  std::thread writer([]{for(int i=0;i<30;++i)arli::redact::remember("rotated-fixture-"+std::to_string(i));});
  for(int i=0;i<100;++i)require(arli::redact::text(secret)=="[redacted]","concurrent registry reads");writer.join();
  require(arli::redact::text(secret)=="[redacted]","old credential kept after rotation");
  std::cout<<"REDACTION_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
