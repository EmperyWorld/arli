#include "tools.hpp"
#include "tools/tier_a.hpp"
#include "tools/git_tools.hpp"
#include "tools/shell_run.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
void require(bool ok,const std::string& message) {if(!ok) throw std::runtime_error(message);}
int main() {try {
  const auto root=fs::temp_directory_path()/("arli-private-"+std::to_string(getpid()));
  fs::create_directories(root/"container/state/workspace");
  const auto state=root/"container/state", wd=state/"workspace";
  setenv("ARLI_STATE_DIR",state.c_str(),1);
  setenv("ARLI_TEST_SECRET","INHERITED_SECRET",1);
  std::ofstream(state/"ui_token")<<"PRIVATE_SENTINEL";
  std::ofstream(root/"public.txt")<<"PUBLIC_SENTINEL\n--pre=literal\n";
  fs::create_directory_symlink(state,root/"alias");
  for(const std::string path:{"container/state/ui_token","alias/ui_token"}) {
    require(!arli::tool_read_file(root,path).ok,"private read");
    require(!arli::tool_append_file(root,path,"changed").ok,"private append");
    require(!arli::tool_copy_file(root,path,"copy.txt").ok,"private copy source");
    require(!arli::tool_copy_file(root,"public.txt",path).ok,"private copy dest");
    require(!arli::tool_move_file(root,path,"moved.txt").ok,"private move");
    require(!arli::tool_delete_file(root,path).ok,"private delete");
    require(!arli::tool_restore_file(root,path).ok,"private restore");
  }
  require(!arli::tool_create_file(root,"container/state/new.txt","x").ok,"private create");
  require(!arli::tool_delete_dir(root,"container").ok,"ancestor delete");
  require(!arli::tool_move_file(root,"container","renamed").ok,"ancestor move");
  require(arli::tool_create_file(wd,"normal.txt","ordinary").ok,"default workspace create");
  require(arli::tool_read_file(wd,"normal.txt").ok,"default workspace read");
  auto shell=arli::tool_run_shell(wd,"cat normal.txt");
  require(shell.ok&&shell.output=="ordinary","default workspace shell: "+shell.output);
  require(!arli::tool_run_shell(state,"cat ui_token").ok,"private cwd shell");
  for(auto result:{arli::tool_glob_files(root,"*",".",{}),arli::tool_search_files(root,"ui_token",".",{}),
       arli::tool_rg_search(root,"PRIVATE_SENTINEL",".",{})})
    require(result.ok&&result.output.find("ui_token")==std::string::npos&&result.output.find("PRIVATE_SENTINEL")==std::string::npos,"recursive disclosure: "+result.output);
  auto rg=arli::tool_rg_search(root,"--pre=literal",".",{});
  require(rg.ok&&rg.output.find("public.txt")!=std::string::npos,"literal option pattern: "+rg.output);
  auto env=arli::tools_internal::run_read_only(root,{"/usr/bin/env"});
  require(env.ok&&env.output.find("INHERITED_SECRET")==std::string::npos,"environment leak");
  auto denied=arli::tools_internal::run_read_only(root,{"/bin/sh","-c","echo bad > public.txt"});
  require(!denied.ok,"readonly write");
  auto init=arli::tool_run_shell(wd,"git init -q && git add normal.txt && git -c user.name=test -c user.email=test@example.invalid commit -qm initial && git config diff.external /nonexistent-helper && git config core.fsmonitor /nonexistent-helper");
  require(init.ok,"git fixture: "+init.output);
  std::ofstream(wd/"normal.txt")<<"changed";
  auto diff=arli::tool_git_diff(wd,"normal.txt");
  require(diff.ok&&diff.output.find("+changed")!=std::string::npos,"git diff: "+diff.output);
  require(arli::tool_git_status(wd).ok,"git status");
  fs::remove_all(root);
  std::cout<<"PRIVATE_STATE_OK\n";
  return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
