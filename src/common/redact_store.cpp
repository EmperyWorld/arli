#include "redact_store.hpp"
#include <openssl/sha.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cerrno>
namespace arli::redact::store {
namespace {
std::mutex mutex;std::filesystem::path ledger;
constexpr std::string_view header="ARLI_REDACTION_SHA256_V1\n";
struct Fd {int value;~Fd(){if(value>=0)close(value);}};
[[noreturn]] void failed(){throw std::runtime_error("REDACTION_STORE_UNAVAILABLE: protected credential fingerprints unavailable");}
int open_file(bool create=false) {
  const int fd=open(ledger.c_str(),O_RDWR|(create?O_CREAT:0)|O_APPEND|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
  struct stat st{};
  if(fd<0)return -1;
  if(fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)||st.st_nlink!=1||st.st_size>128*1024){close(fd);return -1;}
  if(flock(fd,LOCK_EX)!=0){close(fd);return -1;}return fd;
}
void write_all(int fd,std::string_view value) {
  size_t offset=0;while(offset<value.size()) {const auto n=write(fd,value.data()+offset,value.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0)failed();offset+=n;}
  if(fsync(fd)!=0)failed();
}
std::vector<Fingerprint> read_all(int fd,bool initialize=false) {
  if(lseek(fd,0,SEEK_SET)<0)failed();std::string data;char buf[4096];
  for(;;){const auto n=read(fd,buf,sizeof(buf));if(n<0&&errno==EINTR)continue;if(n<0)failed();if(!n)break;data.append(buf,n);if(data.size()>128*1024)failed();}
  if(data.empty()){if(!initialize)failed();write_all(fd,header);return {};}
  if(data.rfind(header,0)!=0||data.back()!='\n')failed();
  std::istringstream input(data.substr(header.size()));std::string line;std::vector<Fingerprint> out;
  while(std::getline(input,line)) {
    Fingerprint record{};std::string hex,extra;std::istringstream fields(line);
    if(!(fields>>record.size>>std::hex>>record.rolling>>hex)||fields>>extra||record.size==0||record.size>65536||hex.size()!=64||out.size()>=512)failed();
    for(size_t i=0;i<32;++i){const auto a=hex[2*i],b=hex[2*i+1];auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};if(digit(a)<0||digit(b)<0)failed();record.digest[i]=digit(a)*16+digit(b);}
    out.push_back(record);
  }
  return out;
}
}
std::uint64_t rolling(std::string_view value){std::uint64_t result=0;for(unsigned char c:value)result=result*131+(c+1);return result;}
Fingerprint fingerprint(std::string_view value){Fingerprint out{value.size(),rolling(value),{}};SHA256(reinterpret_cast<const unsigned char*>(value.data()),value.size(),out.digest.data());return out;}
void enable(const std::filesystem::path& path) {
  std::lock_guard lock(mutex);const auto normalized=std::filesystem::absolute(path).lexically_normal();
  if(!ledger.empty()&&ledger!=normalized)failed();ledger=normalized;
  Fd file{open_file(true)};if(file.value<0)failed();(void)read_all(file.value,true);
  // Persist creation of the ledger directory entry as well as its contents.
  Fd parent{open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC)};
  if(parent.value<0||fsync(parent.value)!=0)failed();
}
void append(std::string_view value) {
  std::lock_guard lock(mutex);if(ledger.empty())return;if(value.empty()||value.size()>65536)failed();
  Fd file{open_file()};if(file.value<0)failed();const auto all=read_all(file.value);const auto wanted=fingerprint(value);
  for(const auto& record:all)if(record.size==wanted.size&&record.digest==wanted.digest)return;
  if(all.size()>=512)failed();
  std::ostringstream line;line<<wanted.size<<' '<<std::hex<<wanted.rolling<<' ';
  for(auto byte:wanted.digest)line<<std::setw(2)<<std::setfill('0')<<static_cast<int>(byte);line<<'\n';write_all(file.value,line.str());
}
std::vector<Fingerprint> records() {
  std::lock_guard lock(mutex);if(ledger.empty())return {};
  Fd file{open_file()};if(file.value<0)failed();return read_all(file.value);
}
}
