#include "mutation_catalog.hpp"
#include "tools/mutation_journal.hpp"
#include "tools/atomic_publish.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(){try{
  char tmp[]="/tmp/arli-catalog-XXXXXX";check(mkdtemp(tmp),"temp");const fs::path root(tmp),wd=root/"workspace";setenv("ARLI_STATE_DIR",tmp,1);fs::create_directories(wd);std::ofstream(wd/"f")<<"before";fs::path manifest;
  {arli::tools_internal::MutationScope scope(wd,11,22,5);check(arli::publish_file(wd/"f","after",true).ok,"indexed write");manifest=scope.path();check(arli::task_changesets(wd,11,22,5).empty(),"active journal excluded");}
  const auto id=manifest.parent_path().filename().string();check(arli::task_changesets(wd,11,22,5)==std::vector<std::string>{id},"closed journal discovered");check(arli::task_changesets(wd,12,22,5).empty()&&arli::task_changesets(wd,11,23,5).empty(),"owner/turn filter");
  const auto dir=manifest.parent_path().parent_path()/"tasks"/"5",marker=dir/(id+".json");const auto bytes=read(marker);
  fs::remove(marker);fs::create_symlink(wd/"f",marker);bool rejected=false;try{arli::task_changesets(wd,11,22,5);}catch(...){rejected=true;}check(rejected,"marker symlink rejected");fs::remove(marker);std::ofstream(marker)<<bytes;chmod(marker.c_str(),0600);
  chmod(dir.c_str(),0777);{arli::tools_internal::MutationScope scope(wd,11,22,5);check(!arli::publish_file(wd/"f","unsafe",true).ok&&read(wd/"f")=="after","unsafe index prevents workspace mutation");}chmod(dir.c_str(),0700);
  for(int n=1;n<64;++n){arli::tools_internal::MutationScope scope(wd,11,22,5);check(arli::publish_file(wd/"f",std::to_string(n),true).ok,"bounded independent attempts");}
  check(arli::task_changesets(wd,11,22,5).size()==64,"all bounded attempts discoverable");const auto before=read(wd/"f");
  {arli::tools_internal::MutationScope scope(wd,11,22,5);check(!arli::publish_file(wd/"f","overflow",true).ok&&read(wd/"f")==before,"quota failure before mutation");}
  fs::remove_all(root);std::cout<<"MUTATION_CATALOG_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
