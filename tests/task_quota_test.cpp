#include "db.hpp"
#include "task_limits.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  char tmp[]="/tmp/arli-task-quota-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);const auto file=(fs::path(tmp)/"quota.db").string();
  arli::Database db(file);db.init();const auto d=db.create_dialog("quota");std::atomic<int> accepted=0,rejected=0,unexpected=0;
  std::vector<std::thread> writers;
  for(int n=0;n<16;++n)writers.emplace_back([&,n]{try{arli::Database conn(file);for(int k=0;k<8;++k){try{conn.enqueue_task(d.id,n*8+k+1,"queue","race-"+std::to_string(n*8+k));++accepted;}catch(const arli::TaskLimitError&){++rejected;}}}catch(...){++unexpected;}});
  for(auto& thread:writers)thread.join();
  check(accepted==arli::kMaxActiveTasks&&rejected==64&&unexpected==0&&db.count_queued()==64,"independent connections atomically admit exactly capacity");
  {arli::Database reopened(file);bool refused=false;try{reopened.enqueue_task(d.id,200,"revert","revert","turn-00000000000000000000000000000000");}catch(const arli::TaskLimitError&){refused=true;}check(refused&&reopened.count_queued()==64,"reopen/revert cannot bypass active quota");}
  const auto old=db.next_queued()->id;db.mark_running(old,"worker");bool refused=false;try{db.enqueue_task(d.id,201,"new");}catch(const arli::TaskLimitError&){refused=true;}check(refused,"running tasks count toward quota");
  db.finish_task(old,"done");check(db.enqueue_task(d.id,202,"new").id>old,"terminal task frees count and bytes");db.fail_active_tasks("test reset");
  const std::string large(arli::kMaxTaskTextBytes,'x');int count=0;int64_t bytes=0;
  for(;;){try{auto task=db.enqueue_task(d.id,300+count,large);bytes+=task.text.size()+task.config_json.size();++count;}catch(const arli::TaskLimitError&){break;}}
  check(count>0&&count<arli::kMaxActiveTasks&&bytes<=arli::kMaxActiveTaskBytes,"byte quota enforced before count limit");db.fail_active_tasks("test reset");
  auto settings=db.get_settings();settings.shell_ask=std::string(arli::kMaxTaskSnapshotBytes,'z');db.set_settings(settings);refused=false;
  try{db.enqueue_task(d.id,400,"small");}catch(const arli::TaskLimitError& e){refused=std::string(e.what()).find("TASK_SNAPSHOT_LIMIT")!=std::string::npos;}
  check(refused&&!db.next_queued(),"oversized snapshot rejected before insertion");
  refused=false;try{db.enqueue_task(d.id,401,large+"x");}catch(const arli::TaskLimitError& e){refused=std::string(e.what()).find("TASK_TEXT_LIMIT")!=std::string::npos;}
  check(refused&&!db.next_queued(),"database boundary also bounds input bytes");std::cout<<"TASK_QUOTA_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
