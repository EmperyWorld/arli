#include "redact.hpp"
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/stubs/strutil.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;
const std::string previous="rotated-away-fixture-credential-0123456789";
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
  if(argc==3){
    arli::redact::enable_persistence(argv[2]);
    if(std::string(argv[1])=="write"){arli::redact::remember(previous);return 0;}
    // This process never called remember(previous): only durable fingerprints exist.
    require(arli::redact::text("before "+previous+" after")=="before [redacted] after","restart fingerprint matching");
    arli::redact::Stream stream;std::string output;
    for(char c:previous)output+=stream.push(std::string(1,c));output+=stream.finish();
    require(output=="[redacted]","old key bytewise stream");
    const std::string unicode="Русский текст ✅ без утечки. "+previous+" Ещё текст ✅ конец.";
    for(size_t cut=1;cut<=unicode.size();++cut) {
      arli::redact::Stream utf8;std::string result;
      for(size_t at=0;at<unicode.size();at+=cut){auto chunk=utf8.push(unicode.substr(at,cut));
        require(google::protobuf::internal::IsStructurallyValidUTF8(chunk),"each emitted chunk is valid UTF-8");result+=chunk;}
      auto tail=utf8.finish();require(google::protobuf::internal::IsStructurallyValidUTF8(tail),"UTF-8 final tail");result+=tail;
      require(result=="Русский текст ✅ без утечки. [redacted] Ещё текст ✅ конец.","unicode bytes preserved around old credential");
    }
    require(arli::redact::text("ordinary content")=="ordinary content","ordinary text unchanged");
    return 0;
  }
  char tmp[]="/tmp/arli-redact-store-XXXXXX";require(mkdtemp(tmp),"temp");const auto path=fs::path(tmp)/"fingerprints";
  auto child=[&](const char* mode){const auto pid=fork();if(pid==0){execl(argv[0],argv[0],mode,path.c_str(),nullptr);_exit(127);}require(pid>0,"fork");int status;waitpid(pid,&status,0);return WIFEXITED(status)&&WEXITSTATUS(status)==0;};
  require(child("write")&&child("read"),"write process then independent read process");
  arli::redact::enable_persistence(path);
  std::ifstream file(path);const std::string bytes{std::istreambuf_iterator<char>(file),{}};
  require(bytes.find(previous)==std::string::npos,"no plaintext ledger");
  fs::rename(path,path.string()+".original");fs::create_symlink(path.string()+".original",path);
  require(arli::redact::text(previous).empty(),"symlink ledger fails closed");fs::remove(path);fs::rename(path.string()+".original",path);
  chmod(path.c_str(),0644);require(arli::redact::text(previous).empty(),"public ledger fails closed");chmod(path.c_str(),0600);
  std::ofstream(path,std::ios::app)<<"partial-record";
  require(arli::redact::text(previous).empty(),"torn append fails closed");
  fs::remove(path);require(arli::redact::text(previous).empty()&&!fs::exists(path),"missing active ledger never silently recreated");
  std::cout<<"REDACTION_STORE_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
