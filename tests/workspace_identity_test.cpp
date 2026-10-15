#include "workspace_identity.hpp"
#include "db.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;using json=nlohmann::json;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main(){try{
  char tmp[]="/tmp/arli-workspace-identity-XXXXXX";check(mkdtemp(tmp),"temp");const fs::path root(tmp),a=root/"A",b=root/"B",alias=root/"alias";fs::create_directory(a);fs::create_directory(b);fs::create_directory_symlink(a,alias);
  arli::Database db((root/"test.db").string());db.init();auto settings=db.get_settings();settings.workdir=alias.string();db.set_settings(settings);const auto dialog=db.create_dialog("pin");auto task=db.enqueue_task(dialog.id,1,"queued");const auto snap=json::parse(task.config_json);
  check(snap["workdir"]==a.string()&&snap["workspace_identity"]["path"]==a.string(),"enqueue resolves alias once");fs::remove(alias);fs::create_directory_symlink(b,alias);
  {arli::WorkspaceLease lease(snap["workdir"].get<std::string>(),snap["workspace_identity"].dump());lease.check();std::ofstream(a/"child")<<"ordinary change";lease.check();
    fs::rename(a,root/"old-A");fs::create_directory(a);bool replaced=false;try{lease.check();}catch(const arli::WorkspaceError&){replaced=true;}check(replaced,"active root replacement rejected despite same path");}
  bool replaced=false;try{arli::WorkspaceLease lease(a,snap["workspace_identity"].dump());}catch(const arli::WorkspaceError&){replaced=true;}check(replaced,"queued replaced root rejected");
  const auto missing=root/"missing"/"deep";const auto identity=arli::capture_workspace(missing);{arli::WorkspaceLease lease(missing,identity.dump());lease.check();}check(fs::is_directory(missing),"missing workspace created below pinned ancestor");
  const auto parent=root/"parent";fs::create_directory(parent);const auto future=parent/"new";const auto pin=arli::capture_workspace(future);fs::rename(parent,root/"old-parent");fs::create_directory(parent);
  replaced=false;try{arli::WorkspaceLease lease(future,pin.dump());}catch(const arli::WorkspaceError&){replaced=true;}check(replaced&&!fs::exists(future),"ancestor replacement refused before mkdir");
  const auto safe=root/"safe";fs::create_directory(safe);const auto jump=safe/"new"/"deep";const auto pending=arli::capture_workspace(jump);fs::create_directory_symlink(b,safe/"new");
  replaced=false;try{arli::WorkspaceLease lease(jump,pending.dump());}catch(const arli::WorkspaceError&){replaced=true;}check(replaced&&!fs::exists(b/"deep"),"late symlink cannot redirect creation");
  {arli::Database reopened((root/"test.db").string());auto row=reopened.get_task(task.id);check(row&&json::parse(row->config_json)["workspace_identity"]==snap["workspace_identity"],"identity persisted without recomputation");}
  fs::remove_all(root);std::cout<<"WORKSPACE_IDENTITY_OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
