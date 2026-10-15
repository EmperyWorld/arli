#include "grpc_registry.hpp"
#include "arli.pb.h"
#include <google/protobuf/descriptor.pb.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void save(const fs::path& path,const std::string& bytes){std::ofstream(path,std::ios::binary)<<bytes;chmod(path.c_str(),0600);}
int main(){try{
  char tmp[]="/tmp/arli-registry-XXXXXX";check(mkdtemp(tmp),"temp");setenv("ARLI_STATE_DIR",tmp,1);
  check(!arli::load_grpc_registry(),"missing directory disables provider");
  const auto dir=fs::path(tmp)/"grpc-tools";fs::create_directory(dir);chmod(dir.c_str(),0700);
  google::protobuf::FileDescriptorSet fds;arli::v1::Dialog::descriptor()->file()->CopyTo(fds.add_file());
  save(dir/"app.pb",fds.SerializeAsString());save(dir/"app.token","fixture-secret-token\n");
  nlohmann::json method={{"tool_name","grpc_write"},{"endpoint","127.0.0.1:1"},{"method","arli.v1.Agent.CreateDialog"},
    {"descriptor_file","app.pb"},{"token_file","app.token"},{"mutating",true},{"deadline_ms",1000}};
  nlohmann::json config={{"version",1},{"methods",nlohmann::json::array({method})}};
  const auto publish=[&]{save(dir/"registry.json",config.dump());};publish();
  auto old=arli::load_grpc_registry();check(old&&old->contains("grpc_write"),"load registry");
  check(old->schemas_json().find("fixture-secret")==std::string::npos,"secret schema leak");
  const auto rejected=[&]{bool bad=false;try{arli::load_grpc_registry();}catch(const std::exception& e){bad=true;check(std::string(e.what()).find("fixture-secret")==std::string::npos,"secret error leak");}check(bad,"invalid registry accepted");};
  for(const auto key:{"endpoint","method","descriptor_file","token_file","mutating","deadline_ms"}) {
    auto backup=config;
    if(std::string(key)=="mutating")config["methods"][0][key]="false";
    else if(std::string(key)=="deadline_ms")config["methods"][0][key]=4294967297LL;
    else config["methods"][0][key]="../fixture-secret";
    publish();rejected();config=backup;publish();
  }
  chmod((dir/"app.token").c_str(),0644);rejected();chmod((dir/"app.token").c_str(),0600);
  fs::rename(dir/"app.token",dir/"original");fs::create_symlink("original",dir/"app.token");rejected();fs::remove(dir/"app.token");
  fs::create_hard_link(dir/"original",dir/"app.token");rejected();fs::remove(dir/"app.token");fs::rename(dir/"original",dir/"app.token");
  chmod(dir.c_str(),0755);rejected();chmod(dir.c_str(),0700);
  save(dir/"registry.json","{fixture-secret-malformed");rejected();publish();
  config["methods"]=nlohmann::json::array();publish();
  check(!arli::load_grpc_registry()->contains("grpc_write")&&old->contains("grpc_write"),"per-turn immutable snapshot and reload");
  fs::rename(dir,fs::path(tmp)/"other");fs::create_directory_symlink("other",dir);rejected();
  fs::remove_all(tmp);std::cout<<"GRPC_REGISTRY_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
