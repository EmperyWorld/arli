#pragma once
#include "redact.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <stdexcept>
#include <unistd.h>
namespace arli {
// Only sanitized, complete UTF-8 chunks reach either log generation.
class JobOutput {
 public:
  explicit JobOutput(std::string path):path_(std::move(path)) {
    fd_=open(path_.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd_<0)throw std::runtime_error("job log unavailable");
  }
  ~JobOutput(){if(fd_>=0)close(fd_);}
  void push(std::string_view bytes){redact::ensure_available();write_safe(filter_.push(bytes));}
  void finish(bool stopped){
    redact::ensure_available();
    // Forced termination may interrupt a credential halfway: never flush that tail.
    if(!stopped)write_safe(filter_.finish());
    if(fsync(fd_)!=0)throw std::runtime_error("job log sync failed");
  }
 private:
  void rotate(){
    if(fsync(fd_)!=0)throw std::runtime_error("job log sync failed");
    close(fd_);fd_=-1;
    if(rename(path_.c_str(),(path_+".1").c_str())!=0)throw std::runtime_error("job log rotation failed");
    fd_=open(path_.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);written_=0;
    if(fd_<0)throw std::runtime_error("job log unavailable");
  }
  void write_safe(std::string_view text){
    while(!text.empty()) {
      size_t count=std::min(text.size(),kLimit-written_);
      if(count<text.size())while(count&&(static_cast<unsigned char>(text[count])&0xc0)==0x80)--count;
      if(!count){rotate();continue;}
      size_t offset=0;
      while(offset<count){const auto n=write(fd_,text.data()+offset,count-offset);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("job log write failed");offset+=n;written_+=n;}
      text.remove_prefix(count);
    }
  }
  static constexpr size_t kLimit=1024*1024;
  std::string path_;int fd_=-1;size_t written_=0;redact::Stream filter_;
};
}
