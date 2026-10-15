#include "job_orphans.hpp"
#include "job_lease.hpp"
#include "job_retention.hpp"
#include "paths.hpp"
#include <sys/stat.h>
#include <ctime>
namespace arli {
void OrphanJobs::sweep(Database& db) {
  const auto root=std::filesystem::weakly_canonical(state_root())/"jobs";
  JobDirectoryLease launch_lease(root);if(!launch_lease)return;
  if(entries_&&root_!=root){closedir(entries_);entries_=nullptr;}
  if(!entries_){
    const int fd=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return;entries_=fdopendir(fd);if(!entries_){close(fd);return;}root_=root;
  }
  for(int count=0;count<64;++count){
    auto* entry=readdir(entries_);
    if(!entry){closedir(entries_);entries_=nullptr;return;}
    const std::string name=entry->d_name;
    if(name=="."||name=="..")continue;
    struct stat st{};
    if(fstatat(dirfd(entries_),name.c_str(),&st,AT_SYMLINK_NOFOLLOW)!=0||!S_ISDIR(st.st_mode))continue;
    // Grace also covers supervisors from older binaries without directory leases.
    if(st.st_mtime>std::time(nullptr)-60)continue;
    const auto log=(root/name/"output.log").string();
    if(!db.job_log_registered(log))remove_job_files(log);
  }
}
}
