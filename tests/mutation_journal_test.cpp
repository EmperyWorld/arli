#include "tools/mutation_journal.hpp"
#include "tools/mutation_recovery.hpp"
#include "tools/mutation_revert.hpp"
#include "tools/atomic_publish.hpp"
#include "tools/tier_a.hpp"
#include "mutation_catalog.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;using json=nlohmann::json;
enum Fault{None,BeforeRename,AfterRename,Commit,Prepare,External,Second};Fault fault=None;bool published=false;fs::path external;
extern "C" int __real_renameat(int,const char*,int,const char*);
extern "C" ssize_t __real_write(int,const void*,size_t);
extern "C" int __wrap_renameat(int from,const char* source,int to,const char* target){
  const std::string name(target);
  if(name=="second.txt"&&fault==Second){errno=EACCES;return -1;}
  if(name=="file.txt"&&fault==BeforeRename)_exit(70);
  if(name=="manifest.json"&&fault==Commit&&published){errno=EIO;return -1;}
  const auto result=__real_renameat(from,source,to,target);
  if(name=="file.txt"&&result==0){published=true;if(fault==AfterRename)_exit(71);}return result;
}
extern "C" ssize_t __wrap_write(int fd,const void* data,size_t size){
  if(fault==External){std::error_code ec;auto path=fs::read_symlink("/proc/self/fd/"+std::to_string(fd),ec).string();if(path.find("/changesets/")!=std::string::npos){fault=None;std::ofstream(external)<<"foreign";}}
  if(fault==Prepare){std::error_code ec;const auto path=fs::read_symlink("/proc/self/fd/"+std::to_string(fd),ec).string();if(path.find("/changesets/")!=std::string::npos){errno=ENOSPC;return -1;}}
  return __real_write(fd,data,size);
}
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void seed(const fs::path& p,const std::string& bytes){std::ofstream(p)<<bytes;}
int main(){try{
  char tmp[]="/tmp/arli-mutation-journal-XXXXXX";check(mkdtemp(tmp),"temp");const fs::path root(tmp),state=root/"state",wd=root/"workspace";setenv("ARLI_STATE_DIR",state.c_str(),1);fs::create_directories(wd);
  seed(wd/"file.txt","before");fs::path manifest;
  const json plan={{"checks",json::array({{{"kind","file_equals"},{"path","file.txt"},{"expected","after"}}})},{"unverifiable",""}};
  {arli::tools_internal::MutationScope scope(wd,80,1,80);fault=Prepare;bool refused=false;try{scope.record_acceptance(plan);}catch(...){refused=true;}fault=None;
    check(refused&&!arli::publish_file(wd/"file.txt","forbidden",true).ok&&read(wd/"file.txt")=="before","plan storage failure fences all subsequent writes");}
  {arli::tools_internal::MutationScope scope(wd,81,1,81);scope.record_acceptance(plan);scope.record_acceptance(plan);check(scope.path().empty(),"criteria alone do not expose an undo action");
    auto changed=plan;changed["unverifiable"]="different";bool refused=false;try{scope.record_acceptance(changed);}catch(...){refused=true;}check(refused&&!arli::publish_file(wd/"file.txt","forbidden",true).ok,"criteria cannot change after durable preparation");}
  const auto plan_child=fork();check(plan_child>=0,"fork plan crash");if(plan_child==0){arli::tools_internal::MutationScope scope(wd,82,1,82);scope.record_acceptance(plan);_exit(74);}int plan_status=0;waitpid(plan_child,&plan_status,0);
  check(WIFEXITED(plan_status)&&WEXITSTATUS(plan_status)==74,"crash after criteria before first tool");const auto prepared=arli::task_journals(wd,82,1,82);
  check(prepared.size()==1&&prepared[0].acceptance==plan&&!prepared[0].has_mutations&&arli::task_changesets(wd,82,1,82).empty(),"crash criteria discoverable without fabricated mutation");
  {arli::tools_internal::MutationScope scope(wd,11,22,33);check(scope.path().empty(),"read-only turn allocates no journal");
    check(arli::publish_file(wd/"file.txt","after",true).ok,"journalled replace");
    check(arli::publish_file(wd/"new.txt","created",false).ok,"journalled create");manifest=scope.path();
    bool busy=false;try{arli::tools_internal::inspect_mutations(wd,manifest.parent_path().filename());}catch(const std::exception& e){busy=std::string(e.what()).find("MUTATION_BUSY")!=std::string::npos;}check(busy,"live journal cannot be recovered");
    auto record=json::parse(read(manifest));check(record["dialog_id"]==11&&record["turn_id"]==22&&record["task_id"]==33,"trusted turn ownership");
    const auto& entries=record["entries"];check(entries.size()==2&&entries[0]["phase"]=="applied"&&entries[1]["phase"]=="applied","publication outcomes durable");
    check(entries[0]["before_hash"]==arli::tools_internal::mutation_hash("before")&&entries[0]["after_hash"]==arli::tools_internal::mutation_hash("after"),"before/after digests");
    check(read(state/entries[0]["before_snapshot"].get<std::string>())=="before"&&!entries[1]["before_exists"].get<bool>(),"original private snapshot and create semantics");
    check(!arli::tool_read_file(wd,manifest.string()).ok,"manifest not accessible to model file tools");
  }
  auto inspect=[&]{return arli::tools_internal::inspect_mutations(wd,manifest.parent_path().filename());};
  check(inspect()["entries"][0]["observation"]=="after"&&inspect()["entries"][0]["snapshot_valid"]==true,"closed journal has verified after and original");
  seed(wd/"file.txt","foreign");check(inspect()["entries"][0]["observation"]=="conflict","foreign edits conflict");
  fs::remove(wd/"new.txt");check(inspect()["entries"][1]["observation"]=="before","missing created file is before");
  fs::create_symlink(wd/"file.txt",wd/"new.txt");check(inspect()["entries"][1]["observation"]=="conflict","symlink cannot masquerade as regular result");fs::remove(wd/"new.txt");
  auto original=json::parse(read(manifest));const auto snapshot=state/original["entries"][0]["before_snapshot"].get<std::string>();
  seed(snapshot,"corrupt");check(inspect()["entries"][0]["snapshot_valid"]==false,"corrupt backup cannot enable recovery");seed(snapshot,"before");
  auto bad=original;bad["entries"][0]["path"]="../escape";seed(manifest,bad.dump());bool rejected=false;try{inspect();}catch(...){rejected=true;}check(rejected,"journal traversal rejected");seed(manifest,original.dump());
  {arli::tools_internal::MutationScope scope(wd,11,22,33);check(arli::publish_file(wd/"file.txt","second attempt",true).ok&&scope.path()!=manifest,"reused IDs preserve prior attempt");}
  fs::path repeated;
  {arli::tools_internal::MutationScope scope(wd,12,23);check(arli::publish_file(wd/"file.txt","A",true).ok,"repeat first");check(arli::publish_file(wd/"file.txt","B",true).ok,"repeat second");repeated=scope.path();}
  auto repetitions=arli::tools_internal::inspect_mutations(wd,repeated.parent_path().filename());
  check(repetitions["entries"][0]["observation"]=="superseded"&&repetitions["entries"][1]["observation"]=="after","repeated same-file mutations compare final state only");
  {arli::tools_internal::MutationScope scope(wd,13,24);check(arli::publish_file(wd/"file.txt","B",true).ok,"no-op publish");repeated=scope.path();}
  check(arli::tools_internal::inspect_mutations(wd,repeated.parent_path().filename())["entries"][0]["observation"]=="unchanged","identical before and after cannot prove whether publication occurred");
  seed(wd/"file.txt","before");
  {arli::tools_internal::MutationScope scope(wd,1,2);fault=Prepare;const auto result=arli::publish_file(wd/"file.txt","after",true);fault=None;
    check(!result.ok&&read(wd/"file.txt")=="before","ENOSPC recording intent prevents mutation");check(!arli::publish_file(wd/"file.txt","retry",true).ok,"journal failure fences remaining turn writes");}
  {arli::tools_internal::MutationScope scope(wd,2,3);published=false;fault=Commit;const auto result=arli::publish_file(wd/"file.txt","after",true);fault=None;
    check(!result.ok&&result.output.rfind("MUTATION_UNCERTAIN",0)==0&&read(wd/"file.txt")=="after","commit failure never reports unchanged file or success");
    const auto entry=json::parse(read(scope.path()))["entries"][0];check(entry["phase"]=="prepared"&&read(state/entry["before_snapshot"].get<std::string>())=="before","durable prepared record and original survive commit failure");}
  seed(wd/"file.txt","original");external=wd/"file.txt";
  {arli::tools_internal::MutationScope scope(wd,3,4);fault=External;auto result=arli::publish_file(wd/"file.txt","replacement",true);fault=None;
    check(!result.ok&&read(external)=="foreign"&&json::parse(read(scope.path()))["entries"][0]["phase"]=="failed","foreign edit during preparation preserved and failed action recorded");}
  seed(wd/"file.txt","original");seed(wd/"second.txt","second original");
  {arli::tools_internal::MutationScope scope(wd,4,5);check(arli::publish_file(wd/"file.txt","first changed",true).ok,"first change applies");fault=Second;auto result=arli::publish_file(wd/"second.txt","second changed",true);fault=None;
    const auto entries=json::parse(read(scope.path()))["entries"];check(!result.ok&&entries.size()==2&&entries[0]["phase"]=="applied"&&entries[1]["phase"]=="failed"&&read(wd/"second.txt")=="second original","partial multi-file outcome is explicit, both originals retained");}
  for(auto crash:{BeforeRename,AfterRename}){
    const auto crashdir=wd/(crash==BeforeRename?"before":"after");fs::create_directories(crashdir);seed(crashdir/"file.txt","original");
    const auto pid=fork();check(pid>=0,"fork");
    if(pid==0){arli::tools_internal::MutationScope scope(crashdir,100+crash,1);fault=crash;arli::publish_file(crashdir/"file.txt","replacement",true);_exit(2);}
    int status=0;waitpid(pid,&status,0);check(WIFEXITED(status)&&WEXITSTATUS(status)==(crash==BeforeRename?70:71),"crash at publication boundary");
    bool found=false;for(const auto& item:fs::recursive_directory_iterator(state/"changesets"))if(item.path().filename()=="manifest.json"){
      const auto record=json::parse(read(item.path()));if(record["dialog_id"]!=100+crash)continue;found=true;const auto& entry=record["entries"][0];
      check(entry["phase"]=="prepared"&&read(state/entry["before_snapshot"].get<std::string>())=="original","crash leaves intent and recoverable original");
      const auto recovered=arli::tools_internal::inspect_mutations(crashdir,item.path().parent_path().filename());
      check(recovered["entries"][0]["observation"]==(crash==BeforeRename?"before":"after")&&recovered["entries"][0]["snapshot_valid"]==true,"crash recovery reconciles actual bytes with intent");
      check(json::parse(read(item.path()))==record,"inspection preserves immutable crash evidence");
      const auto bytes=read(crashdir/"file.txt");check(bytes==(crash==BeforeRename?"original":"replacement"),"observed crash state matches recorded before/after digest");
    }check(found,"crashed turn journal discoverable");
  }
  const auto undo_dir=wd/"undo";fs::create_directories(undo_dir);seed(undo_dir/"file.txt","original");seed(undo_dir/"second.txt","second original");fs::path undo_manifest;
  {arli::tools_internal::MutationScope scope(undo_dir,77,1);check(arli::publish_file(undo_dir/"file.txt","one",true).ok,"undo seed one");check(arli::publish_file(undo_dir/"file.txt","two",true).ok,"undo seed repeated");check(arli::publish_file(undo_dir/"second.txt","second changed",true).ok,"undo seed second");check(arli::publish_file(undo_dir/"created.txt","created",false).ok,"undo seed create");undo_manifest=scope.path();}
  const auto undo_id=undo_manifest.parent_path().filename().string();seed(undo_dir/"second.txt","foreign edit");
  auto undone=arli::tools_internal::revert_mutations(undo_dir,undo_id,77,2);
  check(!undone["complete"].get<bool>()&&read(undo_dir/"file.txt")=="original"&&read(undo_dir/"second.txt")=="foreign edit"&&!fs::exists(undo_dir/"created.txt"),"partial rollback preserves foreign file and restores other paths");
  auto undo_record=arli::tools_internal::inspect_mutations(undo_dir,undone["journal"].get<std::string>());
  check(undo_record["reverts"]==undo_id&&undo_record["entries"].size()==2,"undo is durably journalled with source link");
  auto retried=arli::tools_internal::revert_mutations(undo_dir,undo_id,77,3);check(retried["journal"]==""&&!retried["complete"].get<bool>(),"retry is idempotent and foreign conflict remains");
  seed(undo_dir/"second.txt","second changed");check(arli::tools_internal::revert_mutations(undo_dir,undo_id,77,4)["complete"].get<bool>()&&read(undo_dir/"second.txt")=="second original","retry completes previously conflicted path only");
  check(arli::tools_internal::revert_mutations(undo_dir,undone["journal"].get<std::string>(),77,5)["complete"].get<bool>()&&read(undo_dir/"file.txt")=="two"&&read(undo_dir/"created.txt")=="created","undo of undo restores deleted creation and replacement");
  bool wrong_owner=false;try{arli::tools_internal::revert_mutations(undo_dir,undo_id,78,6);}catch(...){wrong_owner=true;}check(wrong_owner,"cross-dialog undo refused");
  // Crash after the first replacement: a fresh retry uses disk state, not optimistic status.
  const auto pid=fork();check(pid>=0,"undo fork");if(pid==0){fault=AfterRename;arli::tools_internal::revert_mutations(undo_dir,undo_id,77,7);_exit(2);}int status=0;waitpid(pid,&status,0);check(WIFEXITED(status)&&WEXITSTATUS(status)==71,"undo crash after publication");
  check(arli::tools_internal::revert_mutations(undo_dir,undo_id,77,8)["complete"].get<bool>()&&read(undo_dir/"file.txt")=="original"&&!fs::exists(undo_dir/"created.txt"),"retry after undo crash completes without clobbering completed paths");
  fs::path failed_undo;
  {arli::tools_internal::MutationScope scope(undo_dir,77,9);check(arli::publish_file(undo_dir/"file.txt","changed",true).ok,"failure fixture one");check(arli::publish_file(undo_dir/"second.txt","second changed",true).ok,"failure fixture two");failed_undo=scope.path();}
  fault=Prepare;auto no_space=arli::tools_internal::revert_mutations(undo_dir,failed_undo.parent_path().filename(),77,10);fault=None;
  check(!no_space["complete"].get<bool>()&&read(undo_dir/"file.txt")=="changed"&&read(undo_dir/"second.txt")=="second changed","undo ENOSPC leaves both current files intact");
  fault=Second;auto denied=arli::tools_internal::revert_mutations(undo_dir,failed_undo.parent_path().filename(),77,11);fault=None;
  check(!denied["complete"].get<bool>()&&read(undo_dir/"file.txt")=="original"&&read(undo_dir/"second.txt")=="second changed","second restore failure is explicit and retryable");
  check(arli::tools_internal::revert_mutations(undo_dir,failed_undo.parent_path().filename(),77,12)["complete"].get<bool>(),"retry failed second restore");
  {arli::tools_internal::MutationScope scope(undo_dir,77,13);check(arli::publish_file(undo_dir/"file.txt","cancel A",true).ok&&arli::publish_file(undo_dir/"second.txt","cancel B",true).ok,"cancel fixture");failed_undo=scope.path();}
  int polls=0;auto interrupted=arli::tools_internal::revert_mutations(undo_dir,failed_undo.parent_path().filename(),77,14,0,[&]{return ++polls>1;});
  check(!interrupted["complete"].get<bool>()&&interrupted["cancelled"].get<bool>()&&read(undo_dir/"file.txt")=="original"&&read(undo_dir/"second.txt")=="cancel B","cancel between files leaves explicit partial result");
  check(arli::tools_internal::revert_mutations(undo_dir,failed_undo.parent_path().filename(),77,15)["complete"].get<bool>(),"retry completes cancelled revert");
  const auto ops=wd/"managed";fs::create_directories(ops/"tree/nested");seed(ops/"delete.txt","delete me");seed(ops/"move.txt","move me");seed(ops/"target.txt","old target");seed(ops/"tree/a.txt","A");seed(ops/"tree/nested/b.txt","B");
  fs::path operation;
  {arli::tools_internal::MutationScope scope(ops,90,1);check(arli::tool_delete_file(ops,"delete.txt").ok,"tracked delete");operation=scope.path();}
  check(!fs::exists(ops/"delete.txt")&&arli::tools_internal::revert_mutations(ops,operation.parent_path().filename(),90,2)["complete"].get<bool>()&&read(ops/"delete.txt")=="delete me","delete journal restores original");
  {arli::tools_internal::MutationScope scope(ops,91,1);check(arli::tool_move_file(ops,"move.txt","target.txt").ok,"tracked move");operation=scope.path();}
  check(!fs::exists(ops/"move.txt")&&read(ops/"target.txt")=="move me","move publishes exact source");
  check(arli::tools_internal::revert_mutations(ops,operation.parent_path().filename(),91,2)["complete"].get<bool>()&&read(ops/"move.txt")=="move me"&&read(ops/"target.txt")=="old target","move journal restores both paths");
  {arli::tools_internal::MutationScope scope(ops,92,1);check(arli::tool_delete_dir(ops,"tree").ok,"bounded tracked tree delete");operation=scope.path();}
  check(!fs::exists(ops/"tree")&&arli::tools_internal::revert_mutations(ops,operation.parent_path().filename(),92,2)["complete"].get<bool>()&&read(ops/"tree/a.txt")=="A"&&read(ops/"tree/nested/b.txt")=="B","tree delete journal restores regular contents");
  std::cout<<"MUTATION_JOURNAL_OK\n";fs::remove_all(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
