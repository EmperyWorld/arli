#include "workspace_identity.hpp"
#include "tools/file_io.hpp"
namespace arli {
namespace fs=std::filesystem;using json=nlohmann::json;using tools_internal::FileFd;using tools_internal::open_parent;
namespace {
void require(bool ok){if(!ok)throw WorkspaceError("WORKSPACE_CHANGED: directory identity does not match queued project");}
json fingerprint(int fd){
  struct statx st{};
  if(statx(fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,STATX_TYPE|STATX_INO|STATX_BTIME,&st)||
     !(st.stx_mask&STATX_INO)||!(st.stx_mask&STATX_BTIME)||!S_ISDIR(st.stx_mode))
    throw WorkspaceError("WORKSPACE_UNAVAILABLE: directory requires statx inode and birth time support");
  return {{"device_major",st.stx_dev_major},{"device_minor",st.stx_dev_minor},{"inode",st.stx_ino},
          {"birth_sec",st.stx_btime.tv_sec},{"birth_nsec",st.stx_btime.tv_nsec}};
}
void valid_path(const fs::path& p){require(p.is_absolute()&&p==p.lexically_normal()&&p.string().find('\0')==std::string::npos);}
}
json capture_workspace(const fs::path& input){
  try{
    require(!input.empty());const auto path=fs::weakly_canonical(fs::absolute(input));auto anchor=path;valid_path(path);
    while(!fs::exists(anchor)){const auto parent=anchor.parent_path();require(parent!=anchor);anchor=parent;}
    FileFd fd(open_parent(anchor/"entry"));
    return {{"version",1},{"path",path.generic_string()},{"anchor",anchor.generic_string()},{"identity",fingerprint(fd.value)}};
  }catch(const WorkspaceError&){throw;}catch(...){throw WorkspaceError("WORKSPACE_UNAVAILABLE: cannot capture project directory identity");}
}
struct WorkspaceLease::Impl {fs::path path;FileFd root;json identity;};
WorkspaceLease::WorkspaceLease(const fs::path& path,const std::string& encoded):impl_(std::make_unique<Impl>()){
  try{
    require(encoded.size()<=16384);const auto snapshot=encoded.empty()?capture_workspace(path):json::parse(encoded);
    require(snapshot.at("version")==1);impl_->path=snapshot.at("path").get<std::string>();const fs::path anchor=snapshot.at("anchor").get<std::string>();
    valid_path(impl_->path);valid_path(anchor);require(fs::absolute(path).lexically_normal()==impl_->path);
    auto relative=impl_->path.lexically_relative(anchor);require(!relative.empty()&&!relative.is_absolute());for(const auto& part:relative)require(part!="..");
    FileFd directory(open_parent(anchor/"entry"));require(fingerprint(directory.value)==snapshot.at("identity"));
    for(const auto& part:relative){
      if(part=="."||part.empty())continue;
      if(mkdirat(directory.value,part.c_str(),0700)==0){require(fsync(directory.value)==0);}else require(errno==EEXIST);
      const int next=openat(directory.value,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);require(next>=0);close(directory.value);directory.value=next;
    }
    impl_->root.value=directory.value;directory.value=-1;impl_->identity=fingerprint(impl_->root.value);check();
  }catch(const WorkspaceError&){throw;}catch(...){throw WorkspaceError("WORKSPACE_CHANGED: project directory unavailable or replaced");}
}
WorkspaceLease::~WorkspaceLease()=default;
void WorkspaceLease::check()const{
  try{FileFd current(open_parent(impl_->path/"entry"));require(fingerprint(current.value)==impl_->identity&&fingerprint(impl_->root.value)==impl_->identity);}
  catch(const WorkspaceError&){throw;}catch(...){throw WorkspaceError("WORKSPACE_CHANGED: project directory moved or replaced");}
}
}
