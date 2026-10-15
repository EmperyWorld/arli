#pragma once
#include <filesystem>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
namespace arli {
// flock belongs to this open description: fork inherits protection until exec.
class JobDirectoryLease {
 public:
  explicit JobDirectoryLease(const std::filesystem::path& path,bool wait=false) {
    fd_=open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(fd_<0)return;
    int result;do{result=flock(fd_,LOCK_EX|(wait?0:LOCK_NB));}while(result<0&&errno==EINTR);
    if(result<0){close(fd_);fd_=-1;}
  }
  ~JobDirectoryLease(){if(fd_>=0)close(fd_);}
  JobDirectoryLease(const JobDirectoryLease&)=delete;
  JobDirectoryLease& operator=(const JobDirectoryLease&)=delete;
  explicit operator bool() const{return fd_>=0;}
  int fd() const{return fd_;}
 private:int fd_=-1;
};
}
