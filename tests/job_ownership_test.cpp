#include "db.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  char tmp[]="/tmp/arli-job-owner-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  const auto path=(fs::path(tmp)/"jobs.db").string();std::int64_t owned=0,dialog_id=0,task_id=0;
  {arli::Database db(path);db.init();const auto d=db.create_dialog("owner"),other=db.create_dialog("other");dialog_id=d.id;
    const auto task=db.enqueue_task(d.id,7,"job");task_id=task.id;db.mark_running(task.id,"test");
    owned=db.add_job(getpid(),"fixture",tmp,"/nonexistent","identity",d.id,task.id,7);
    for(int i=0;i<60;++i){auto id=db.add_job(getpid(),"other",tmp,"/nonexistent","identity",other.id,0,1);db.finish_job(id,"exited",0);}
    auto rows=db.list_jobs(1,d.id,task.id);check(rows.size()==1&&rows[0].id==owned&&rows[0].turn_id==7,"owner filter precedes limit");
    check(db.list_jobs(40,other.id,task.id).empty(),"both owner filters enforced");
    bool rejected=false;try{db.add_job(getpid(),"bad",tmp,"/nonexistent","identity",other.id,task.id,7);}catch(...){rejected=true;}check(rejected,"cross-dialog task rejected");
    db.finish_task(task.id,"done","");rejected=false;try{db.add_job(getpid(),"stale",tmp,"/nonexistent","identity",d.id,task.id,7);}catch(...){rejected=true;}check(rejected,"finished task cannot launch new job");
  }
  {arli::Database db(path);db.init();auto job=db.get_job(owned);check(job&&job->dialog_id==dialog_id&&job->task_id==task_id&&job->turn_id==7,"ownership persists across reopen");}
  // Rebuild only jobs into the actual v12 shape; all other v12 tables remain present.
  sqlite3* raw=nullptr;check(sqlite3_open(path.c_str(),&raw)==SQLITE_OK,"open migration fixture");
  const char* sql="DROP INDEX jobs_owner; ALTER TABLE jobs RENAME TO jobs_new; "
    "CREATE TABLE jobs(id INTEGER PRIMARY KEY AUTOINCREMENT,pid INTEGER NOT NULL,command TEXT NOT NULL,workdir TEXT NOT NULL,log_path TEXT NOT NULL,status TEXT NOT NULL,started_at TEXT NOT NULL,process_identity TEXT NOT NULL DEFAULT '',exit_code INTEGER NOT NULL DEFAULT -1);"
    "INSERT INTO jobs SELECT id,pid,command,workdir,log_path,status,started_at,process_identity,exit_code FROM jobs_new; DROP TABLE jobs_new; UPDATE schema_version SET version=12;";
  check(sqlite3_exec(raw,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"v12 fixture");sqlite3_close(raw);
  {arli::Database db(path);db.init();check(db.schema_version()==16,"migration to 16");auto old=db.get_job(owned);
    check(old&&old->command=="fixture"&&old->dialog_id==0&&old->task_id==0&&old->turn_id==0,"legacy rows preserved as unowned");}
  std::cout<<"JOB_OWNERSHIP_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
