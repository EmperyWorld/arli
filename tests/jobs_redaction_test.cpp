#include "jobs.hpp"
#include "db.hpp"
#include "redact.hpp"
#include <google/protobuf/stubs/common.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
namespace fs=std::filesystem;
void pause_ms(int ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
  char tmp[]="/tmp/arli-jobs-redaction-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  const fs::path root(tmp),wd=root/"workspace";fs::create_directories(wd);
  const std::string secret="background-fixture-secret-0123456789";
  setenv("ARLI_INTERNAL_TOKEN",secret.c_str(),1);arli::redact::remember(secret);
  arli::Database db((root/"jobs.db").string());db.init();
  auto wait=[&](arli::JobRow job){for(int n=0;n<200;++n){job=arli::refresh_job(db,job);if(job.status!="running")break;pause_ms(25);}return job;};
  auto launch=[&](const char* file){auto result=arli::execute_shell(db,wd,std::string("python3 ")+file,"",true);check(result.ok,"supervisor startup");return db.list_jobs().front();};
  std::ofstream(wd/"split.py")<<"import os,sys,time\ns='"<<secret<<"'\nassert 'ARLI_INTERNAL_TOKEN' not in os.environ\nsys.stdout.write(s[:17]);sys.stdout.flush()\nopen('half','w').close()\ntime.sleep(.25)\nsys.stderr.write(s[17:]+'\\nГотово ✅');sys.stderr.flush()\n";
  auto job=launch("split.py");
  for(int n=0;n<100&&!fs::exists(wd/"half");++n)pause_ms(10);
  check(fs::exists(wd/"half"),"partial child output emitted");
  check(read(job.log_path).find(secret.substr(0,10))==std::string::npos,"partial credential held before disk");
  job=wait(job);check(job.status=="exited"&&job.exit_code==0,"split job exits");
  check(read(job.log_path)=="[redacted]\nГотово ✅","stdout/stderr chunks sanitized before disk with exact unicode");
  std::ofstream(wd/"rotate.py")<<"import sys\ns='"<<secret<<"'\nfor i in range(40000):\n sys.stdout.write('Строка ✅ '+s+'\\n')\nsys.stdout.write('END')\n";
  job=wait(launch("rotate.py"));check(job.status=="exited"&&job.exit_code==0,"rotation job exits");
  check(fs::exists(job.log_path+".1"),"log rotation occurred");
  for(const auto& path:{job.log_path,job.log_path+".1"}){const auto output=read(path);check(output.size()<=1048576,"rotation budget");check(output.find(secret)==std::string::npos,"both files clean");check(google::protobuf::internal::IsStructurallyValidUTF8(output),"each log generation valid UTF-8");}
  check(read(job.log_path).ends_with("END"),"final tail flushed");
  std::ofstream(wd/"stop.py")<<"import sys,time\nsys.stdout.write('"<<secret.substr(0,17)<<"');sys.stdout.flush()\nopen('stop-ready','w').close()\ntime.sleep(30)\n";
  job=launch("stop.py");for(int n=0;n<100&&!fs::exists(wd/"stop-ready");++n)pause_ms(10);
  check(arli::stop_job_by_id(db,job.id),"stop supervisor");job=wait(job);
  check(job.status=="stopped"&&read(job.log_path).empty(),"forced stop discards possible secret prefix");
  // A filtering failure must stop the owned process, without writing its pending bytes.
  std::ofstream(wd/"failure.py")<<"import sys,time\nf=open('heartbeat','a')\nwhile True:\n f.write('x');f.flush();sys.stdout.write('"<<secret<<"');sys.stdout.flush();time.sleep(.03)\n";
  job=launch("failure.py");for(int n=0;n<100&&!fs::exists(wd/"heartbeat");++n)pause_ms(10);
  std::ofstream(root/"redaction.fingerprints",std::ios::app)<<"torn record";
  job=wait(job);check(job.status=="lost","filter failure never invents success");
  const auto size=fs::file_size(wd/"heartbeat");pause_ms(150);check(fs::file_size(wd/"heartbeat")==size,"owned child stopped on filter failure");
  check(read(job.log_path).find(secret)==std::string::npos,"filter failure did not expose pending output");
  std::cout<<"JOBS_REDACTION_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
