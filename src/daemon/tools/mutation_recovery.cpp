#include "mutation_recovery.hpp"
#include "mutation_journal.hpp"
#include "backup_store.hpp"
#include "file_io.hpp"
#include "private_state.hpp"
#include <sys/file.h>
#include <map>
namespace arli::tools_internal {
namespace fs=std::filesystem;using json=nlohmann::json;
namespace {
bool hex(const std::string& s,size_t n){return s.size()==n&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
void require(bool ok){if(!ok)throw std::runtime_error("MUTATION_RECOVERY: invalid journal");}
void directory(const fs::path& path,bool strict=true){
  FileFd fd(open_parent(path/"entry"));struct stat st{};
  require(!fstat(fd.value,&st)&&st.st_uid==geteuid()&&!(st.st_mode&(strict?0077:0022)));
}
std::string manifest_bytes(int parent){
  FileFd fd(openat(parent,"manifest.json",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));struct stat st{};
  require(fd.value>=0&&!fstat(fd.value,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_nlink==1&&st.st_size<=4*1024*1024);
  return read_fd(fd.value);
}
std::string observed(const fs::path& file,const json& entry){
  try{
    FileFd parent(open_parent(file));struct stat st{};
    if(fstatat(parent.value,file.filename().c_str(),&st,AT_SYMLINK_NOFOLLOW)){
      if(errno==ENOENT)return !entry["after_exists"].get<bool>()?"after":entry["before_exists"].get<bool>()?"conflict":"before";
      return "unavailable";
    }
    if(!S_ISREG(st.st_mode))return "conflict";
    const auto hash=mutation_hash(read_file_bytes(file));
    const bool before=entry["before_exists"].get<bool>()&&hash==entry["before_hash"].get<std::string>();
    const bool after=entry["after_exists"].get<bool>()&&hash==entry["after_hash"].get<std::string>();
    return before&&after?"unchanged":before?"before":after?"after":"conflict";
  }catch(...){return "unavailable";}
}
}
static json inspect(const fs::path& workdir,const std::string& id,const std::function<void(const json&)>& callback){
  require(id.rfind("turn-",0)==0&&hex(id.substr(5),32));
  const auto wd=fs::canonical(workdir),root=fs::weakly_canonical(state_root());
  const auto project=root/"changesets"/mutation_hash(wd.generic_string());
  directory(root,false);directory(root/"changesets");directory(project);directory(project/id);
  FileFd lease(open_parent(project/id/"entry"));
  if(flock(lease.value,(callback?LOCK_EX:LOCK_SH)|LOCK_NB))throw std::runtime_error("MUTATION_BUSY: changeset is active");
  auto record=json::parse(manifest_bytes(lease.value));
  require(record.at("version")==1&&record.at("id")==id&&record.at("workdir")==wd.generic_string());
  if(record.contains("acceptance_plan"))require(record["acceptance_plan"].is_object()&&record["acceptance_plan"].dump().size()<=65536&&record.at("acceptance_hash")==mutation_hash(record["acceptance_plan"].dump()));
  for(const auto* key:{"dialog_id","turn_id","task_id"})require(record.at(key).is_number_integer()&&record.at(key).get<int64_t>()>=0);
  auto& entries=record.at("entries");require(entries.is_array()&&entries.size()<=4096);
  std::map<std::string,size_t> last;
  for(size_t i=0;i<entries.size();++i){
    auto& e=entries[i];const auto name=e.at("path").get<std::string>();const fs::path rel(name);
    require(!name.empty()&&name.find('\0')==std::string::npos&&!rel.is_absolute()&&rel!="."&&rel==rel.lexically_normal());
    for(const auto& part:rel)require(part!="..");
    require(!private_state_path(wd/rel));
    require(e.at("sequence")==i+1&&e.at("before_exists").is_boolean()&&e.at("after_exists").is_boolean());
    const bool before=e["before_exists"].get<bool>();
    const bool after=e["after_exists"].get<bool>();require(before||after);
    require(e.at("operation")== (!after?"delete":before?"replace":"create"));
    require(after?hex(e.at("after_hash").get<std::string>(),64):e.at("after_hash")=="");
    const auto phase=e.at("phase").get<std::string>();require(phase=="prepared"||phase=="applied"||phase=="failed"||phase=="uncertain");
    const auto snapshot=e.at("before_snapshot").get<std::string>();
    if(before){
      require(hex(e.at("before_hash").get<std::string>(),64));
      const auto prefix=backup_path(wd,wd/rel).lexically_relative(root).generic_string()+".history-";
      require(snapshot.rfind(prefix,0)==0&&hex(snapshot.substr(prefix.size()),32));
      try{
        prepare_backup_store(wd,wd/rel,false);
        e["snapshot_valid"]=mutation_hash(read_file_bytes(root/snapshot))==e["before_hash"].get<std::string>();
      }catch(...){e["snapshot_valid"]=false;}
    }else{require(snapshot.empty()&&e.at("before_hash")=="");e["snapshot_valid"]=true;}
    // Only the final non-failed publication for a path describes current bytes.
    // Failed attempts are kept as evidence but cannot supersede a prior write.
    e["observation"]=phase=="failed"?"not_published":"superseded";
    if(phase!="failed")last[name]=i;
  }
  for(const auto& [name,i]:last)entries[i]["observation"]=observed(wd/name,entries[i]);
  if(callback)callback(record);
  return record;
}
json inspect_mutations(const fs::path& wd,const std::string& id){return inspect(wd,id,{});}
void locked_mutations(const fs::path& wd,const std::string& id,const std::function<void(const json&)>& callback){inspect(wd,id,callback);}
}
