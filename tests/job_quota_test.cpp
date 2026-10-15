#include "db.hpp"
#include "job_policy.hpp"
#include "jobs.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  char tmp[]="/tmp/arli-job-quota-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  arli::Database db((fs::path(tmp)/"jobs.db").string());db.init();
  const auto first=db.create_dialog("first"),second=db.create_dialog("second");
  auto task=[&](std::int64_t dialog,int turn){auto t=db.enqueue_task(dialog,turn,"quota");db.mark_running(t.id,"test");return t;};
  const auto a=task(first.id,1),b=task(first.id,2),c=task(first.id,3),d=task(second.id,1),e=task(second.id,2);
  auto add=[&](const arli::TaskRow& t){return db.add_job(getpid(),"fixture",tmp,"/nonexistent","fixture",t.dialog_id,t.id,t.turn_id);};
  auto rejected=[&](const arli::TaskRow& t){try{add(t);return false;}catch(const std::exception& error){return std::string(error.what()).find("JOB_ADMISSION_REJECTED")!=std::string::npos;}};
  for(int n=0;n<arli::kMaxTaskJobs;++n)add(a);
  check(!db.job_capacity_available(first.id,a.id)&&rejected(a),"per-task quota enforced in database");
  for(int n=0;n<arli::kMaxTaskJobs;++n)add(b);
  check(!db.job_capacity_available(first.id,c.id)&&rejected(c),"per-dialog quota enforced across tasks");
  for(int n=0;n<arli::kMaxTaskJobs;++n){add(d);add(e);}
  check(!db.job_capacity_available(),"global quota includes all dialogs");
  bool denied=false;try{db.add_job(getpid(),"unowned",tmp,"/nonexistent");}catch(...){denied=true;}check(denied,"unowned jobs cannot bypass global quota");
  const auto job_dir=fs::path(tmp)/"jobs";
  const auto before=std::distance(fs::directory_iterator(job_dir),fs::directory_iterator{});
  auto denied_start=arli::execute_shell(db,tmp,"touch should-not-run","",true);
  check(!denied_start.ok&&!fs::exists(fs::path(tmp)/"should-not-run")&&
    std::distance(fs::directory_iterator(job_dir),fs::directory_iterator{})==before,"quota denial creates no supervisor directory or command effect");
  const auto old=db.list_jobs().front().id;db.finish_job(old,"exited",0);
  check(db.job_capacity_available()&&db.add_job(getpid(),"unowned",tmp,"/nonexistent")>old,"terminal job releases capacity");
  const auto race=(fs::path(tmp)/"race.db").string();{arli::Database init(race);init.init();}
  std::atomic<int> accepted=0;std::vector<std::thread> writers;
  for(int i=0;i<32;++i)writers.emplace_back([&]{try{arli::Database conn(race);conn.add_job(getpid(),"race",tmp,"/nonexistent");++accepted;}catch(...){}});
  for(auto& writer:writers)writer.join();
  arli::Database verify(race);
  check(accepted==arli::kMaxRunningJobs&&verify.list_jobs(100).size()==arli::kMaxRunningJobs,"concurrent independent connections never oversubscribe quota");
  std::cout<<"JOB_QUOTA_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
