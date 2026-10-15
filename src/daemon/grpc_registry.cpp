#include "grpc_registry.hpp"
#include "paths.hpp"
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <set>
#include <stdexcept>
namespace arli {
namespace {
struct Fd {int value;~Fd(){if(value>=0)close(value);}};
[[noreturn]] void invalid() {throw std::runtime_error("GRPC_REGISTRY_INVALID: проверьте защищённый реестр gRPC-инструментов");}
bool same(const struct stat& a,const struct stat& b) {
  return a.st_size==b.st_size&&a.st_mtim.tv_sec==b.st_mtim.tv_sec&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec&&
    a.st_ctim.tv_sec==b.st_ctim.tv_sec&&a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
std::string read_private(int dir,const std::string& name,size_t maximum,size_t& remaining) {
  if(name.empty()||name.size()>128||name=="."||name=="..")invalid();
  for(const unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))invalid();
  Fd file{openat(dir,name.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
  struct stat before{},after{};
  if(file.value<0||fstat(file.value,&before)!=0||!S_ISREG(before.st_mode)||before.st_uid!=geteuid()||
      (before.st_mode&0077)!=0||before.st_nlink!=1||before.st_size<0||static_cast<size_t>(before.st_size)>maximum||
      static_cast<size_t>(before.st_size)>remaining)invalid();
  std::string bytes(static_cast<size_t>(before.st_size),'\0');size_t offset=0;
  while(offset<bytes.size()) {
    auto n=read(file.value,bytes.data()+offset,bytes.size()-offset);
    if(n<0&&errno==EINTR)continue;
    if(n<=0)invalid();offset+=static_cast<size_t>(n);
  }
  char extra; if(read(file.value,&extra,1)!=0||fstat(file.value,&after)!=0||!same(before,after))invalid();
  remaining-=bytes.size();return bytes;
}
void keys(const nlohmann::json& object,const std::set<std::string>& allowed) {
  if(!object.is_object())invalid();for(auto it=object.begin();it!=object.end();++it)if(!allowed.count(it.key()))invalid();
}
}
std::unique_ptr<GrpcToolProvider> load_grpc_registry() {
  const auto path=state_root()/"grpc-tools";
  Fd dir{open(path.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW)};
  if(dir.value<0) {if(errno==ENOENT)return {};invalid();}
  try {
    struct stat status{};
    if(fstat(dir.value,&status)!=0||status.st_uid!=geteuid()||(status.st_mode&0077)!=0)invalid();
    size_t remaining=16*1024*1024;
    const auto registry=nlohmann::json::parse(read_private(dir.value,"registry.json",128*1024,remaining));
    keys(registry,{"version","methods"});
    if(!registry.at("version").is_number_integer()||registry.at("version").get<std::int64_t>()!=1||
        !registry.at("methods").is_array()||registry.at("methods").size()>32)invalid();
    auto provider=std::make_unique<GrpcToolProvider>();
    for(const auto& item:registry.at("methods")) {
      keys(item,{"tool_name","endpoint","method","descriptor_file","token_file","mutating","deadline_ms"});
      if(!item.at("mutating").is_boolean()||!item.at("deadline_ms").is_number_integer())invalid();
      GrpcToolMethod spec;
      spec.tool_name=item.at("tool_name").get<std::string>();spec.endpoint=item.at("endpoint").get<std::string>();
      spec.method=item.at("method").get<std::string>();spec.mutating=item.at("mutating").get<bool>();
      const auto timeout=item.at("deadline_ms").get<std::int64_t>();if(timeout<1||timeout>60000)invalid();
      spec.deadline_ms=static_cast<int>(timeout);
      spec.descriptor_set=read_private(dir.value,item.at("descriptor_file").get<std::string>(),4*1024*1024,remaining);
      spec.bearer_token=read_private(dir.value,item.at("token_file").get<std::string>(),8192,remaining);
      if(!spec.bearer_token.empty()&&spec.bearer_token.back()=='\n')spec.bearer_token.pop_back();
      provider->add(std::move(spec));
    }
    return provider;
  }catch(...) {invalid();} // Never put configuration bytes or credentials in task events/logs.
}
}
