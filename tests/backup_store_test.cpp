#include "tools/backup_store.hpp"
#include "tools/tier_a.hpp"
#include "tools/shell_run.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string read(const fs::path& p){std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int main(){try{
  char tmp[]="/tmp/arli-backup-store-XXXXXX";check(mkdtemp(tmp),"temp");const fs::path root(tmp),state=root/"state",one=root/"one",two=root/"two";
  setenv("ARLI_STATE_DIR",state.c_str(),1);fs::create_directories(one);fs::create_directories(two);
  std::ofstream(one/"file.txt")<<"first project";std::ofstream(two/"file.txt")<<"second project";
  check(arli::tool_edit_lines(one,"file.txt",1,1,"changed one").ok,"first protected edit");
  check(arli::tool_edit_lines(two,"file.txt",1,1,"changed two").ok,"second protected edit");
  const auto a=arli::tools_internal::backup_path(one,one/"file.txt"),b=arli::tools_internal::backup_path(two,two/"file.txt");
  check(a!=b&&read(a)=="first project"&&read(b)=="second project","same relative filename isolated by project");
  check(!fs::exists(one/".arli-bak")&&!fs::exists(two/".arli-bak"),"no new backup inside model workspace");
  for(auto p=a.parent_path();p!=root;p=p.parent_path()){struct stat st{};check(stat(p.c_str(),&st)==0&&(st.st_mode&0077)==0&&st.st_uid==geteuid(),"all state/backup ancestors private");}
  fs::create_directory_symlink(one,root/"alias");check(arli::tool_restore_file(root/"alias","file.txt").ok&&read(one/"file.txt")=="first project","canonical project alias resolves same snapshot");
  check(arli::tool_restore_file(two,"file.txt").ok&&read(two/"file.txt")=="second project","restore stays within project namespace");
  fs::create_directory_symlink(a.parent_path(),one/"backup-link");
  check(!arli::tool_read_file(one,"backup-link/latest").ok,"read through workspace alias denied");
  check(!arli::tool_edit_lines(one,"backup-link/latest",1,1,"poison").ok&&read(a)=="first project","backup cannot be poisoned through file tool");
  check(!arli::tool_delete_dir(one,"backup-link").ok&&read(a)=="first project","recursive mutation cannot delete private backups");
  auto shell=arli::tool_run_shell(one,"test ! -e '"+a.string()+"'");check(shell.ok,"shell sandbox cannot see private backup");
  fs::create_directories(one/".arli-bak");std::ofstream(one/".arli-bak/legacy.txt.bak")<<"untrusted";
  check(!arli::tool_restore_file(one,"legacy.txt").ok&&!fs::exists(one/"legacy.txt")&&read(one/".arli-bak/legacy.txt.bak")=="untrusted","legacy workspace backup neither trusted nor removed");
  chmod((state/"backups").c_str(),0755);const auto before=read(one/"file.txt");
  check(!arli::tool_edit_lines(one,"file.txt",1,1,"unsafe").ok&&read(one/"file.txt")==before,"unsafe store permissions reject mutation");chmod((state/"backups").c_str(),0700);
  fs::rename(a,a.string()+"-saved");fs::create_symlink(two/"file.txt",a);
  check(!arli::tool_restore_file(one,"file.txt").ok&&read(one/"file.txt")==before,"latest symlink never followed");fs::remove(a);fs::rename(a.string()+"-saved",a);
  const std::string long_name(255,'x');std::ofstream(one/long_name)<<"long original";
  check(arli::tool_edit_lines(one,long_name,1,1,"long changed").ok&&arli::tool_restore_file(one,long_name).ok&&read(one/long_name)=="long original","maximum filename supported by hashed storage key");
  std::cout<<"BACKUP_STORE_OK\n";fs::remove_all(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
