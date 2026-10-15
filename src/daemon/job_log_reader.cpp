#include "job_log_reader.hpp"
#include "paths.hpp"
#include <charconv>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <stdexcept>
namespace arli {
std::string JobLogPosition::encode() const{return generation.empty()?"":generation+":"+std::to_string(offset);}
JobLogPosition JobLogPosition::parse(const std::string& encoded) {
  if(encoded.empty())return {};
  if(encoded.size()>180)throw std::invalid_argument("invalid job log position");
  size_t begin=0;std::uint64_t value=0;
  for(int i=0;i<6;++i){
    const auto end=encoded.find(':',begin);const auto stop=end==std::string::npos?encoded.size():end;
    const auto parsed=std::from_chars(encoded.data()+begin,encoded.data()+stop,value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=encoded.data()+stop||stop==begin||(i<5)==(end==std::string::npos))throw std::invalid_argument("invalid job log position");
    begin=stop+1;
  }
  if(value>1048576)throw std::invalid_argument("invalid job log offset");
  return {encoded.substr(0,encoded.rfind(':')),value};
}
namespace {
struct Fd {int value=-1;~Fd(){if(value>=0)close(value);}Fd()=default;explicit Fd(int fd):value(fd){}Fd(const Fd&)=delete;};
struct Generation {Fd fd;std::string key;std::uint64_t size=0;};
void open_generation(int dir,const char* name,Generation& out) {
  out.fd.value=openat(dir,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
  if(out.fd.value<0){if(errno==ENOENT)return;throw std::runtime_error("job log unavailable");}
  struct statx st{};
  if(statx(out.fd.value,"",AT_EMPTY_PATH,STATX_BASIC_STATS|STATX_BTIME,&st)!=0||!(st.stx_mask&STATX_BTIME)||
     !S_ISREG(st.stx_mode)||st.stx_uid!=geteuid()||st.stx_nlink!=1||(st.stx_mode&0077)||st.stx_size>1048576)
    throw std::runtime_error("job log identity unavailable");
  out.key=std::to_string(st.stx_dev_major)+":"+std::to_string(st.stx_dev_minor)+":"+std::to_string(st.stx_ino)+":"+
    std::to_string(st.stx_btime.tv_sec)+":"+std::to_string(st.stx_btime.tv_nsec);out.size=st.stx_size;
}
}
JobLogSlice read_job_log(const std::string& name,const JobLogPosition& after) {
  namespace fs=std::filesystem;const fs::path path(name);const auto root=fs::weakly_canonical(state_root());
  if(!path.is_absolute()||path!=path.lexically_normal()||path.parent_path().parent_path()!=root/"jobs"||path.filename()!="output.log")throw std::runtime_error("job log path unavailable");
  const auto leaf=path.parent_path().filename().string();if(leaf.size()!=10||leaf.rfind("job-",0)!=0)throw std::runtime_error("job log path unavailable");
  Fd state(open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  Fd jobs(openat(state.value,"jobs",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  Fd dir(openat(jobs.value,leaf.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  struct stat ds{};if(dir.value<0||fstat(dir.value,&ds)||ds.st_uid!=geteuid()||(ds.st_mode&0077))throw std::runtime_error("job log directory unavailable");
  // Open current first. A concurrent rename can make .1 point to the same inode;
  // de-duplicate it rather than replaying the same generation twice.
  std::array<Generation,2> files;bool stable=false;
  for(int attempt=0;attempt<3&&!stable;++attempt){
    for(auto& file:files){if(file.fd.value>=0)close(file.fd.value);file.fd.value=-1;file.key.clear();file.size=0;}
    open_generation(dir.value,"output.log",files[1]);open_generation(dir.value,"output.log.1",files[0]);
    Generation check;open_generation(dir.value,"output.log",check);stable=check.key==files[1].key;
  }
  if(!stable)throw JobLogBusy("job log rotated during snapshot");
  if(!files[1].key.empty()&&files[0].key==files[1].key)files[0].key.clear();
  int chosen=-1;for(int i=0;i<2;++i)if(!after.generation.empty()&&files[i].key==after.generation)chosen=i;
  JobLogSlice result;result.start=after;result.next=after;
  if(chosen<0){
    for(int i=0;i<2;++i)if(!files[i].key.empty()){chosen=i;break;}
    if(chosen<0){result.end=true;return result;}
    result.gap=!after.generation.empty();result.start={files[chosen].key,0};
  }
  auto position=result.start;
  if(position.offset>files[chosen].size){result.gap=true;position.offset=0;result.start=position;}
  if(position.offset==files[chosen].size&&chosen==0&&!files[1].key.empty()){chosen=1;position={files[1].key,0};result.start=position;}
  const auto count=std::min<std::uint64_t>(8192,files[chosen].size-position.offset);
  result.text.resize(count);ssize_t n=0;
  if(count){do{n=pread(files[chosen].fd.value,result.text.data(),count,position.offset);}while(n<0&&errno==EINTR);if(n<0)throw std::runtime_error("job log read failed");result.text.resize(n);}
  result.next={position.generation,position.offset+result.text.size()};
  result.end=result.next.offset==files[chosen].size&&(chosen==1||files[1].key.empty());return result;
}
}
