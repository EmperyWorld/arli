#include "db.hpp"
#include "jobs.hpp"
#include "job_orphans.hpp"
#include "job_lease.hpp"
#include "job_channel.hpp"
#include "job_identity.hpp"
#include "job_retention.hpp"
#include "tools/process_policy.hpp"
#include "redact.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <sys/stat.h>
#include <sys/wait.h>
#include <csignal>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void age(const fs::path& p){timespec t[2]={{1000000000,0},{1000000000,0}};check(utimensat(AT_FDCWD,p.c_str(),t,AT_SYMLINK_NOFOLLOW)==0,"age directory");}
void pause_ms(int ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
int main(){try{
  char tmp[]="/tmp/arli-job-orphans-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  const fs::path root(tmp),wd=root/"workspace",jobs=root/"jobs";fs::create_directories(wd);
  arli::Database db((root/"arli.db").string());db.init();arli::OrphanJobs sweep;
  auto directory=[&]{auto path=(jobs/"job-XXXXXX").string();check(mkdtemp(path.data()),"directory");return fs::path(path);};
  auto run=[&]{for(int n=0;n<4;++n)sweep.sweep(db);};
  const auto recent=directory();run();check(fs::exists(recent),"recent orphan grace");
  const auto registered=directory();const auto id=db.add_job(0,"registered",wd.string(),(registered/"output.log").string());age(registered);
  const auto legacy=directory();db.add_job(0,"legacy",wd.string(),(fs::path("legacy/jobs")/legacy.filename()/"output.log").string());age(legacy);
  const auto unknown=directory();std::ofstream(unknown/"operator-note")<<"keep";age(unknown);
  const auto orphan=directory();std::ofstream(orphan/"output.log")<<"abandoned";age(orphan);
  run();check(!fs::exists(orphan),"abandoned directory removed");check(fs::exists(registered)&&db.get_job(id)&&fs::exists(unknown)&&fs::exists(legacy),"registered and unknown files preserved");
  // Abrupt launcher death before helper exec: parent-directory lease covers the window.
  const auto staged=jobs/"job-ABC123";int signal_pipe[2];check(pipe(signal_pipe)==0,"pipe");
  auto launcher=fork();check(launcher>=0,"launcher fork");
  if(launcher==0){close(signal_pipe[0]);arli::JobDirectoryLease lease(jobs,true);if(!lease)_exit(2);if(mkdir(staged.c_str(),0700))_exit(3);age(staged);char ready='R';write(signal_pipe[1],&ready,1);for(;;)pause();}
  close(signal_pipe[1]);char ready=0;check(read(signal_pipe[0],&ready,1)==1&&ready=='R',"launcher staging");close(signal_pipe[0]);
  run();check(fs::exists(staged),"launch lease prevents unregistered cleanup");
  kill(launcher,SIGKILL);waitpid(launcher,nullptr,0);run();check(!fs::exists(staged),"SIGKILL releases launcher lease and orphan reclaimed");
  // Real supervisor waiting for GO. Kill its launcher after R, without inserting a DB row.
  for(bool registered_before_go:{false,true}){
  const auto waiting=directory();const auto ledger=root/"redaction.fingerprints";arli::redact::enable_persistence(ledger);
  const auto launch=arli::prepare_shell(wd,"touch must-not-run",{},false);
  const std::string config=nlohmann::json{{"cwd",launch.cwd},{"argv",launch.argv},{"env",launch.env},{"log",(waiting/"output.log").string()},{"fingerprints",ledger.string()}}.dump();
  const auto helper=fs::read_symlink("/proc/self/exe").parent_path()/"arli-job-supervisor";
  check(pipe(signal_pipe)==0,"supervisor pipe");launcher=fork();check(launcher>=0,"supervisor launcher fork");
  if(launcher==0){
    close(signal_pipe[0]);int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))_exit(2);
    auto child=fork();if(child==0){dup2(pair[1],3);fcntl(3,F_SETFD,0);close_range(4,~0U,0);execl(helper.c_str(),helper.c_str(),nullptr);_exit(127);}
    close(pair[1]);arli::JobChannel channel(pair[0]);auto encoded=config;uint32_t size=encoded.size();char ack=0;
    if(!channel.transfer(&size,sizeof(size),true)||!channel.transfer(encoded.data(),encoded.size(),true)||!channel.transfer(&ack,1,false)||ack!='R')_exit(3);
    write(signal_pipe[1],&child,sizeof(child));for(;;)pause();
  }
  close(signal_pipe[1]);pid_t supervisor=0;check(read(signal_pipe[0],&supervisor,sizeof(supervisor))==sizeof(supervisor)&&supervisor>0,"real supervisor ready");close(signal_pipe[0]);age(waiting);
  run();check(fs::exists(waiting)&&!arli::remove_job_files((waiting/"output.log").string()),"live supervisor lease blocks both cleanup paths");
  std::int64_t staged_id=0;if(registered_before_go)staged_id=db.add_job(supervisor,"before GO",wd.string(),(waiting/"output.log").string(),arli::process_identity(supervisor));
  kill(launcher,SIGKILL);waitpid(launcher,nullptr,0);
  for(int n=0;n<200&&!arli::process_identity(supervisor).empty();++n)pause_ms(10);
  check(arli::process_identity(supervisor).empty(),"supervisor exits on dead launcher before GO");
  run();check(!fs::exists(wd/"must-not-run"),"pre-GO crash never executes command");
  if(registered_before_go){auto job=arli::refresh_job(db,*db.get_job(staged_id));check(job.status=="lost"&&job.exit_code==-1&&fs::exists(waiting),"registered pre-GO crash becomes lost, files retained by lifecycle");}
  else check(!fs::exists(waiting),"unregistered pre-GO orphan reclaimed");
  }
  // Multiple pages: skip entries cannot starve subsequent abandoned directories.
  for(int i=0;i<140;++i){auto dir=directory();age(dir);}
  run();size_t count=0;for(const auto& entry:fs::directory_iterator(jobs)){(void)entry;++count;}
  check(count==5,"bounded scan eventually reclaims all pages");
  // Production launcher after GO: a background task survives its worker, remains stoppable.
  check(pipe(signal_pipe)==0,"post-GO pipe");launcher=fork();check(launcher>=0,"post-GO fork");
  if(launcher==0){
    close(signal_pipe[0]);arli::Database child_db((root/"arli.db").string());child_db.init();
    auto result=arli::execute_shell(child_db,wd,"while true; do echo tick >> heartbeat; sleep 0.03; done","",true);
    if(!result.ok)_exit(2);auto job=child_db.list_jobs().front();write(signal_pipe[1],&job.id,sizeof(job.id));for(;;)pause();
  }
  close(signal_pipe[1]);std::int64_t background=0;check(read(signal_pipe[0],&background,sizeof(background))==sizeof(background),"production launch GO");close(signal_pipe[0]);
  for(int n=0;n<100&&!fs::exists(wd/"heartbeat");++n)pause_ms(10);
  check(fs::exists(wd/"heartbeat"),"background command running");kill(launcher,SIGKILL);waitpid(launcher,nullptr,0);
  auto running=*db.get_job(background);age(fs::path(running.log_path).parent_path());const auto before=fs::file_size(wd/"heartbeat");run();pause_ms(150);
  check(fs::file_size(wd/"heartbeat")>before&&fs::exists(running.log_path),"registered supervisor survives worker SIGKILL and orphan sweep");
  check(arli::stop_job_by_id(db,background),"orphaned running supervisor remains controllable through pidfd");
  const auto after=fs::file_size(wd/"heartbeat");pause_ms(100);check(fs::file_size(wd/"heartbeat")==after,"post-crash stop kills command tree");
  std::cout<<"JOB_ORPHANS_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
