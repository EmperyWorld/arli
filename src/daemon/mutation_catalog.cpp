#include "mutation_catalog.hpp"
#include "tools/file_io.hpp"
#include "tools/atomic_publish.hpp"
#include "tools/mutation_journal.hpp"
#include "paths.hpp"
#include <dirent.h>
#include <sys/file.h>
#include <algorithm>
namespace arli {
namespace fs=std::filesystem;using json=nlohmann::json;using tools_internal::FileFd;using tools_internal::open_parent;
namespace {
void require(bool yes){if(!yes)throw std::runtime_error("MUTATION_CATALOG: unavailable or invalid metadata");}
bool valid(const std::string& id){return id.size()==37&&id.rfind("turn-",0)==0&&id.substr(5).find_first_not_of("0123456789abcdef")==std::string::npos;}
void directory(const fs::path& p,bool create,bool strict=true){FileFd fd(open_parent(p/"entry",create));struct stat st{};require(!fstat(fd.value,&st)&&st.st_uid==geteuid()&&!(st.st_mode&(strict?0077:0022)));}
fs::path project(const fs::path& wd){return fs::weakly_canonical(state_root())/"changesets"/tools_internal::mutation_hash(fs::canonical(wd).generic_string());}
std::vector<std::string> entries(int fd){
  DIR* raw=fdopendir(dup(fd));require(raw);struct Close{DIR* p;~Close(){closedir(p);}}close{raw};std::vector<std::string> out;
  errno=0;while(auto* item=readdir(raw)){const std::string name=item->d_name;if(name=="."||name=="..")continue;require(out.size()<64);out.push_back(name);errno=0;}require(errno==0);std::sort(out.begin(),out.end());return out;
}
json read_marker(int parent,const std::string& name){
  FileFd fd(openat(parent,name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat st{};
  require(fd.value>=0&&!fstat(fd.value,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_nlink==1&&st.st_size<=4096);
  return json::parse(tools_internal::read_fd(fd.value));
}
}
void register_changeset(const fs::path& wd,const std::string& id,int64_t dialog,int64_t turn,int64_t task){
  if(task<=0)return;require(valid(id)&&dialog>0&&turn>0);const auto base=project(wd),dir=base/"tasks"/std::to_string(task);
  directory(fs::weakly_canonical(state_root()),false,false);directory(base.parent_path(),false);directory(base,false);directory(base/"tasks",true);directory(dir,true);
  FileFd lease(open_parent(dir/"entry"));require(!flock(lease.value,LOCK_EX));
  const json marker={{"version",1},{"id",id},{"dialog_id",dialog},{"turn_id",turn},{"task_id",task}};
  const auto names=entries(lease.value);const auto name=id+".json";
  if(std::find(names.begin(),names.end(),name)!=names.end()){require(read_marker(lease.value,name)==marker);return;}
  require(names.size()<64);require(publish_file(dir/name,marker.dump(),false).ok);
}
std::vector<TaskJournal> task_journals(const fs::path& wd,int64_t dialog,int64_t turn,int64_t task){
  if(task<=0||dialog<=0||turn<=0)return {};const auto base=project(wd),dir=base/"tasks"/std::to_string(task);
  std::error_code ec;if(!fs::exists(dir,ec)&&!ec)return {};
  directory(fs::weakly_canonical(state_root()),false,false);directory(base.parent_path(),false);directory(base,false);directory(base/"tasks",false);directory(dir,false);
  FileFd lease(open_parent(dir/"entry"));require(!flock(lease.value,LOCK_SH|LOCK_NB));std::vector<TaskJournal> out;
  for(const auto& name:entries(lease.value)){
    require(name.size()==42&&name.substr(37)==".json"&&valid(name.substr(0,37)));const auto id=name.substr(0,37);const auto m=read_marker(lease.value,name);
    require(m.at("version")==1&&m.at("id")==id&&m.at("task_id")==task);
    if(m.at("dialog_id")!=dialog||m.at("turn_id")!=turn)continue;
    directory(base/id,false);FileFd journal(open_parent(base/id/"entry"));
    if(flock(journal.value,LOCK_SH|LOCK_NB)){if(errno==EWOULDBLOCK)continue;require(false);}
    FileFd manifest(openat(journal.value,"manifest.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));struct stat st{};
    require(manifest.value>=0&&!fstat(manifest.value,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_nlink==1&&st.st_size<=4*1024*1024);
    const auto record=json::parse(tools_internal::read_fd(manifest.value));
    require(record.at("id")==id&&record.at("version")==1&&record.at("task_id")==task&&record.at("dialog_id")==dialog&&record.at("turn_id")==turn&&record.at("workdir")==fs::canonical(wd).generic_string());
    require(record.at("entries").is_array()&&record["entries"].size()<=4096);
    TaskJournal info{id,!record["entries"].empty(),nullptr};
    if(record.contains("acceptance_plan")){
      info.acceptance=record.at("acceptance_plan");require(info.acceptance.is_object()&&info.acceptance.dump().size()<=65536);
      require(record.at("acceptance_hash")==tools_internal::mutation_hash(info.acceptance.dump()));
    }
    out.push_back(std::move(info));
  }return out;
}
std::vector<std::string> task_changesets(const fs::path& wd,int64_t dialog,int64_t turn,int64_t task){
  std::vector<std::string> out;for(const auto& info:task_journals(wd,dialog,turn,task))if(info.has_mutations)out.push_back(info.id);return out;
}
}
