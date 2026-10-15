#include "job_retention.hpp"
#include "job_identity.hpp"
#include "paths.hpp"
#include "log.hpp"
#include <filesystem>
#include <set>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
namespace arli {
namespace {
struct Fd{int value;~Fd(){if(value>=0)close(value);}};
const std::set<std::string> names={"output.log","output.log.1","output.log.result","output.log.result.tmp"};
bool private_dir(int fd){struct stat st{};return fd>=0&&fstat(fd,&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&(st.st_mode&0077)==0;}
}
bool remove_job_files(const std::string& log_path) try {
  namespace fs=std::filesystem;
  const auto root=fs::weakly_canonical(state_root());const fs::path path(log_path);
  if(!path.is_absolute()||path!=path.lexically_normal()||path.filename()!="output.log"||path.parent_path().parent_path()!=root/"jobs")return false;
  const auto name=path.parent_path().filename().string();if(name.size()!=10||name.rfind("job-",0)!=0)return false;
  for(size_t i=4;i<name.size();++i)if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='A'&&name[i]<='Z')||(name[i]>='0'&&name[i]<='9')))return false;
  Fd state{open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};
  if(state.value<0)return false;
  Fd jobs{openat(state.value,"jobs",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};
  if(jobs.value<0)return false;
  Fd job{openat(jobs.value,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};
  if(job.value<0)return errno==ENOENT;
  if(!private_dir(job.value)||flock(job.value,LOCK_EX|LOCK_NB)!=0)return false;
  struct stat held{};if(fstat(job.value,&held)!=0)return false;
  DIR* entries=fdopendir(dup(job.value));if(!entries)return false;
  bool safe=true;errno=0;
  while(auto* entry=readdir(entries)) {
    const std::string child=entry->d_name;if(child=="."||child=="..")continue;
    struct stat st{};
    if(!names.count(child)||fstatat(job.value,child.c_str(),&st,AT_SYMLINK_NOFOLLOW)!=0||(!S_ISREG(st.st_mode)&&!S_ISLNK(st.st_mode))){safe=false;break;}
  }
  if(errno)safe=false;closedir(entries);if(!safe)return false;
  for(const auto& child:names)if(unlinkat(job.value,child.c_str(),0)!=0&&errno!=ENOENT)return false;
  if(fsync(job.value)!=0)return false;
  struct stat current{};
  if(fstatat(jobs.value,name.c_str(),&current,AT_SYMLINK_NOFOLLOW)!=0||held.st_dev!=current.st_dev||held.st_ino!=current.st_ino)return false;
  if(unlinkat(jobs.value,name.c_str(),AT_REMOVEDIR)!=0)return false;
  return fsync(jobs.value)==0;
}catch(...){return false;}
void retain_jobs(Database& db,std::int64_t& cursor) {
  const auto page=db.expired_jobs_after(cursor,32);
  for(const auto& job:page){
    cursor=job.id;const auto identity=process_identity(job.pid);
    if(!identity.empty()&&(job.process_identity.empty()||identity==job.process_identity))continue;
    if(remove_job_files(job.log_path))db.forget_job(job);
    else log::warn("jobs","retention skipped unsafe/unavailable directory for job="+std::to_string(job.id));
  }
  if(page.size()<32)cursor=0;
}
}
