#include "lifecycle_harness.hpp"
#include "jobs.hpp"
#include "db.hpp"
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <sys/stat.h>
#include <fcntl.h>
using namespace lt;
namespace fs=std::filesystem;
int main(int argc,char** argv){if(argc!=3)return 2;
  char tmp[]="/tmp/arli-job-maintenance-XXXXXX";if(!mkdtemp(tmp))return 2;setenv("ARLI_STATE_DIR",tmp,1);
  const fs::path wd=fs::path(tmp)/"workspace";fs::create_directories(wd);
  arli::Database db((fs::path(tmp)/"arli.db").string());db.init();
  auto started=arli::execute_shell(db,wd,"sleep 0.4; printf done; exit 7","",true);LT_CHECK(started.ok,"background fixture starts");
  const auto id=db.list_jobs().front().id;
  const auto stale=db.add_job(getpid(),"stale",wd.string(),"/nonexistent","foreign-boot:1");
  const auto ep="127.0.0.1:"+std::to_string(free_port());Proc daemon;
  daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});
  Client client(ep);LT_CHECK(client.wait_health(8000),"daemon health");
  // No ListJobs / refresh_job calls: observe the persisted database directly.
  for(int n=0;n<160;++n){auto job=db.get_job(id);if(job&&job->status=="exited")break;sleep_ms(25);}
  const auto completed=db.get_job(id);LT_CHECK(completed&&completed->status=="exited"&&completed->exit_code==7,"daemon reconciles terminal result without UI");
  LT_CHECK(db.get_job(stale)->status=="lost"&&pid_alive(getpid()),"stale identity marked lost without signal");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();
  sqlite3* raw=nullptr;sqlite3_open((fs::path(tmp)/"arli.db").c_str(),&raw);
  const auto age_sql="UPDATE jobs SET finished_at='2000-01-01T00:00:00Z' WHERE id="+std::to_string(id);
  LT_CHECK(sqlite3_exec(raw,age_sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"age completed fixture");sqlite3_close(raw);
  const auto orphan=fs::path(tmp)/"jobs/job-ORPHAN";LT_CHECK(mkdir(orphan.c_str(),0700)==0,"orphan fixture");
  timespec old[2]={{1000000000,0},{1000000000,0}};utimensat(AT_FDCWD,orphan.c_str(),old,0);
  started=arli::execute_shell(db,wd,"printf restart; exit 3","",true);LT_CHECK(started.ok,"offline job starts");const auto offline=db.list_jobs().front().id;sleep_ms(200);
  LT_CHECK(db.get_job(offline)->status=="running","row initially waits for reconciliation");
  daemon.start(argv[1],{ep},{{"ARLI_STATE_DIR",tmp},{"ARLI_AGENT_BIN",argv[2]},{"ARLI_LISTEN",ep}});
  LT_CHECK(client.wait_health(8000),"daemon restart");
  for(int n=0;n<160;++n){if(db.get_job(offline)->status=="exited")break;sleep_ms(25);}
  LT_CHECK(db.get_job(offline)->exit_code==3&&db.get_job(offline)->status=="exited","restart reconciliation does not require opening jobs UI");
  LT_CHECK(!fs::exists(orphan),"daemon restart reclaims old unregistered directory without UI");
  LT_CHECK(!db.get_job(id)&&completed&&!fs::exists(completed->log_path),"daemon retention removes expired files and row without UI");
  daemon.term();if(daemon.wait(8000)<0)daemon.kill9();
  std::cout<<"JOB_MAINTENANCE_"<<(g_failed?"FAIL":"OK")<<'\n';return g_failed?1:0;
}
