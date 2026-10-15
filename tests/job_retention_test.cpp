#include "db.hpp"
#include "jobs.hpp"
#include "job_identity.hpp"
#include "job_retention.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void age(const std::string& path,std::int64_t id) {
  sqlite3* raw=nullptr;check(sqlite3_open(path.c_str(),&raw)==SQLITE_OK,"age open");
  const auto sql="UPDATE jobs SET finished_at='2000-01-01T00:00:00Z' WHERE id="+std::to_string(id);
  check(sqlite3_exec(raw,sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,"age row");sqlite3_close(raw);
}
int main(){try{
  char tmp[]="/tmp/arli-job-retention-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  const fs::path root(tmp),wd=root/"workspace";const auto path=(root/"jobs.db").string();arli::Database db(path);db.init();
  auto directory=[&]{auto name=(root/"jobs/job-XXXXXX").string();check(mkdtemp(name.data()),"job directory");std::ofstream(fs::path(name)/"output.log")<<"fixture";return fs::path(name);};
  auto row=[&](const fs::path& file,bool alive=false){auto id=db.add_job(alive?getpid():0,"fixture",wd.string(),file.string(),alive?arli::process_identity(getpid()):"");db.finish_job(id,"exited",7);return id;};
  const auto recent_dir=directory();const auto recent=row(recent_dir/"output.log");
  const auto expired_dir=directory();const auto expired=row(expired_dir/"output.log");age(path,expired);
  db.finish_job(expired,"exited",7);check(db.get_job(expired)->finished_at=="2000-01-01T00:00:00Z","repeated terminal update does not renew retention");
  const auto alive_dir=directory();const auto alive=row(alive_dir/"output.log",true);age(path,alive);
  const auto active_dir=directory();const auto active=db.add_job(getpid(),"active",wd.string(),(active_dir/"output.log").string(),arli::process_identity(getpid()));age(path,active);
  const auto external=wd/"external.txt";std::ofstream(external)<<"keep";const auto outside=row(external);age(path,outside);
  const auto alias=root/"jobs/job-ABC123";fs::create_directory_symlink(wd,alias);const auto aliased=row(alias/"output.log");age(path,aliased);std::ofstream(wd/"output.log")<<"outside";
  const auto unknown_dir=directory();std::ofstream(unknown_dir/"operator-note")<<"keep";const auto unknown=row(unknown_dir/"output.log");age(path,unknown);
  const auto linked_dir=directory();fs::remove(linked_dir/"output.log");fs::create_symlink(external,linked_dir/"output.log");const auto linked=row(linked_dir/"output.log");age(path,linked);
  const auto partial_dir=directory();const auto partial=row(partial_dir/"output.log");fs::remove(partial_dir/"output.log");std::ofstream(partial_dir/"output.log.result")<<"partial";age(path,partial);
  std::int64_t cursor=0;arli::retain_jobs(db,cursor);
  check(!db.get_job(expired)&&!fs::exists(expired_dir),"old terminal job and files removed");
  check(db.get_job(recent)&&fs::exists(recent_dir),"recent history preserved");
  check(db.get_job(alive)&&fs::exists(alive_dir)&&db.get_job(active)&&fs::exists(active_dir),"live identity and running jobs preserved");
  check(db.get_job(outside)&&fs::exists(external)&&db.get_job(aliased)&&fs::exists(wd/"output.log"),"outside path and directory alias never followed");
  check(db.get_job(unknown)&&fs::exists(unknown_dir/"output.log"),"unknown contents preserve whole directory");
  check(!db.get_job(linked)&&fs::exists(external),"file symlink removed without touching target");
  check(!db.get_job(partial)&&!fs::exists(partial_dir),"retry after partial file cleanup succeeds");
  // Count retention in an independent DB, without unsafe-path exceptions.
  arli::Database count_db((root/"count.db").string());count_db.init();
  for(int i=0;i<arli::kRetainedJobs+5;++i){const auto dir=directory();auto id=count_db.add_job(0,"count",wd.string(),(dir/"output.log").string());count_db.finish_job(id,"exited",0);}
  cursor=0;arli::retain_jobs(count_db,cursor);check(count_db.list_jobs(1000).size()==arli::kRetainedJobs,"terminal count bounded independently of age");
  // A failed owner check occurs after supervisor readiness, before GO.
  const auto before=std::distance(fs::directory_iterator(root/"jobs"),fs::directory_iterator{});
  arli::ShellRunOptions options;options.dialog_id=999;options.task_id=999;options.turn_id=1;
  auto failed=arli::execute_shell(db,wd,"touch must-not-run","",true,options);
  check(!failed.ok&&!fs::exists(wd/"must-not-run")&&before==std::distance(fs::directory_iterator(root/"jobs"),fs::directory_iterator{}),"failed pre-GO launch cleans its private directory");
  std::cout<<"JOB_RETENTION_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
