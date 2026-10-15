#include "mutation_revert.hpp"
#include "mutation_recovery.hpp"
#include "mutation_journal.hpp"
#include "atomic_publish.hpp"
#include "file_io.hpp"
#include "paths.hpp"
#include <map>
namespace arli::tools_internal {
namespace fs=std::filesystem;using json=nlohmann::json;
namespace {
ToolResult remove_checked(const fs::path& file,const std::string& expected){
  MutationTicket ticket;bool removed=false;
  try{
    ticket=prepare_mutation(file,{},true,&expected,false);
    FileFd dir(open_parent(file));FileFd fd(openat(dir.value,file.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
    struct stat before{},current{},anchor{},resolved{};
    if(fd.value<0||fstat(fd.value,&before)||read_fd(fd.value)!=expected)throw std::runtime_error("FILE_CHANGED: cannot remove changed file");
    FileFd now(open_parent(file));
    if(fstat(dir.value,&anchor)||fstat(now.value,&resolved)||anchor.st_dev!=resolved.st_dev||anchor.st_ino!=resolved.st_ino||
       fstatat(dir.value,file.filename().c_str(),&current,AT_SYMLINK_NOFOLLOW)||current.st_dev!=before.st_dev||current.st_ino!=before.st_ino||
       current.st_ctim.tv_sec!=before.st_ctim.tv_sec||current.st_ctim.tv_nsec!=before.st_ctim.tv_nsec||read_fd(fd.value)!=expected)
      throw std::runtime_error("FILE_CHANGED: file or parent moved before removal");
    // Same residual POSIX check-to-mutation race as the publisher; not a CAS.
    if(unlinkat(dir.value,file.filename().c_str(),0))throw std::runtime_error("remove failed");
    removed=true;if(fsync(dir.value))throw std::runtime_error("REMOVED_UNSYNCED: directory sync failed");
    return finish_mutation(ticket,{true,{}},true);
  }catch(const std::exception& e){return finish_mutation(ticket,{false,e.what()},removed);}
}
struct Chain {json first,last;bool connected=true;};
}
json revert_mutations(const fs::path& workdir,const std::string& id,int64_t dialog,int64_t turn,int64_t task,const std::function<bool()>& cancelled){
  const auto wd=fs::canonical(workdir);json report={{"source",id},{"files",json::array()},{"complete",true},{"journal",""}};
  locked_mutations(wd,id,[&](const json& record){
    if(record.at("dialog_id")!=dialog)throw std::runtime_error("MUTATION_OWNER: changeset belongs to another dialog");
    std::map<std::string,Chain> chains;
    for(const auto& e:record["entries"]){
      if(e["phase"]=="failed")continue;
      const auto path=e["path"].get<std::string>();auto [it,inserted]=chains.try_emplace(path,Chain{e,e,true});
      if(!inserted){auto& c=it->second;c.connected=c.connected&&c.last["after_exists"]==e["before_exists"]&&c.last["after_hash"]==e["before_hash"];c.last=e;}
    }
    MutationScope undo(wd,dialog,turn,task,id);
    for(const auto& [name,chain]:chains){
      if(cancelled&&cancelled()){report["complete"]=false;report["cancelled"]=true;break;}
      std::string outcome="conflict",error;
      try{
        if(!chain.connected)throw std::runtime_error("intervening changes between journal entries");
        const auto file=wd/name;const bool want=chain.first["before_exists"].get<bool>();
        // A tracked tree deletion also removed the parent directories.  Recreate
        // those parents through the no-follow directory walk before inspecting
        // the final component; otherwise every nested file is incorrectly
        // reported as a conflict and a directory deletion cannot be reverted.
        FileFd parent(open_parent(file,want));struct stat st{};const int rc=fstatat(parent.value,file.filename().c_str(),&st,AT_SYMLINK_NOFOLLOW);
        if(rc&&errno!=ENOENT)throw std::runtime_error("cannot inspect current file");
        const bool exists=rc==0;if(exists&&!S_ISREG(st.st_mode))throw std::runtime_error("current path is not regular");
        const auto current=exists?read_file_bytes(file):std::string();const auto hash=exists?mutation_hash(current):std::string();
        if(exists==want&&hash==chain.first["before_hash"].get<std::string>())outcome="already_original";
        else{
          const bool matches_after=exists==chain.last["after_exists"].get<bool>()&&hash==chain.last["after_hash"].get<std::string>();
          const bool uncertain=chain.last["phase"]=="prepared"||chain.last["phase"]=="uncertain";
          const bool matches_before=uncertain&&exists==chain.last["before_exists"].get<bool>()&&hash==chain.last["before_hash"].get<std::string>();
          if(!matches_after&&!matches_before)throw std::runtime_error("current bytes contain external changes");
          ToolResult result;
          if(want){
            if(!chain.first["snapshot_valid"].get<bool>())throw std::runtime_error("original snapshot unavailable");
            const auto snapshot=fs::weakly_canonical(state_root())/chain.first["before_snapshot"].get<std::string>();
            const auto bytes=read_file_bytes(snapshot);
            if(mutation_hash(bytes)!=chain.first["before_hash"].get<std::string>())throw std::runtime_error("original snapshot changed");
            result=publish_file(file,bytes,exists,exists?&current:nullptr,&snapshot);
          }else result=remove_checked(file,current);
          outcome=result.ok?"reverted":"error";error=result.output;
        }
      }catch(const std::exception& e){error=e.what();}
      if(outcome=="conflict"||outcome=="error")report["complete"]=false;
      report["files"].push_back({{"path",name},{"outcome",outcome},{"error",error}});
    }
    if(!undo.path().empty())report["journal"]=undo.path().parent_path().filename().string();
  });return report;
}
}
