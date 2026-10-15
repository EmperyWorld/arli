#include "tier_a.hpp"
#include "private_state.hpp"

#include "backup.hpp"
#include "backup_store.hpp"
#include "atomic_publish.hpp"
#include "file_io.hpp"
#include "mutation_journal.hpp"
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <vector>

namespace arli {
namespace {

using tools_internal::backup_regular_file;

std::string rel_string(const std::filesystem::path& root, const std::filesystem::path& abs) {
  std::error_code ec;
  const auto rel = std::filesystem::relative(abs, root, ec);
  return ec ? abs.filename().string() : rel.generic_string();
}

ToolResult remove_regular(const std::filesystem::path& file,const std::string& expected){
  tools_internal::MutationTicket ticket;bool removed=false;
  try{
    ticket=tools_internal::prepare_mutation(file,{},true,&expected,false);
    tools_internal::FileFd dir(tools_internal::open_parent(file));
    tools_internal::FileFd fd(openat(dir.value,file.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
    struct stat before{},current{},anchor{},resolved{};
    if(fd.value<0||fstat(fd.value,&before)||!S_ISREG(before.st_mode)||tools_internal::read_fd(fd.value)!=expected)
      throw std::runtime_error("FILE_CHANGED: cannot remove changed regular file");
    tools_internal::FileFd now(tools_internal::open_parent(file));
    if(fstat(dir.value,&anchor)||fstat(now.value,&resolved)||anchor.st_dev!=resolved.st_dev||anchor.st_ino!=resolved.st_ino||
       fstatat(dir.value,file.filename().c_str(),&current,AT_SYMLINK_NOFOLLOW)||current.st_dev!=before.st_dev||current.st_ino!=before.st_ino||
       current.st_ctim.tv_sec!=before.st_ctim.tv_sec||current.st_ctim.tv_nsec!=before.st_ctim.tv_nsec||tools_internal::read_fd(fd.value)!=expected)
      throw std::runtime_error("FILE_CHANGED: file or parent moved before removal");
    if(unlinkat(dir.value,file.filename().c_str(),0))throw std::runtime_error("remove failed");
    removed=true;if(fsync(dir.value))throw std::runtime_error("REMOVED_UNSYNCED: directory sync failed");
    return tools_internal::finish_mutation(ticket,{true,{}},true);
  }catch(const std::exception& e){return tools_internal::finish_mutation(ticket,{false,e.what()},removed);}
}

ToolResult remove_empty_dir(const std::filesystem::path& dir){
  try{
    tools_internal::FileFd parent(tools_internal::open_parent(dir));
    tools_internal::FileFd child(openat(parent.value,dir.filename().c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    struct stat before{},current{},anchor{},resolved{};
    if(child.value<0||fstat(child.value,&before))return {false,"cannot anchor directory"};
    tools_internal::FileFd now(tools_internal::open_parent(dir));
    if(fstat(parent.value,&anchor)||fstat(now.value,&resolved)||anchor.st_dev!=resolved.st_dev||anchor.st_ino!=resolved.st_ino||
       fstatat(parent.value,dir.filename().c_str(),&current,AT_SYMLINK_NOFOLLOW)||current.st_dev!=before.st_dev||current.st_ino!=before.st_ino)
      return {false,"FILE_CHANGED: directory moved before removal"};
    if(unlinkat(parent.value,dir.filename().c_str(),AT_REMOVEDIR))return {false,std::string("directory remove failed: ")+std::strerror(errno)};
    if(fsync(parent.value))return {false,"REMOVED_UNSYNCED: directory sync failed"};return {true,{}};
  }catch(const std::exception& e){return {false,e.what()};}
}

}  // namespace

ToolResult tool_make_dir(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto dir = resolve_under(workdir, path);
    std::filesystem::create_directories(dir);
    return {true, "created " + rel_string(workdir, dir)};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_copy_file(const std::filesystem::path& workdir, const std::string& src, const std::string& dest) {
  try {
    const auto from = resolve_under(workdir, src);
    const auto to = resolve_under(workdir, dest);
    if (!std::filesystem::is_regular_file(from)) {
      return {false, "source not a file: " + from.string()};
    }
    const auto content = tools_internal::read_file_bytes(from);
    tools_internal::FileFd parent(tools_internal::open_parent(to, true));
    const bool exists = std::filesystem::exists(to);
    const auto before = exists ? tools_internal::read_file_bytes(to) : std::string();
    if (exists) {
      const auto backup = backup_regular_file(workdir, to, &before);
      if (!backup.ok) return backup;
    }
    const auto result = publish_file(to, content, exists, exists ? &before : nullptr, exists ? nullptr : &from);
    if (!result.ok) return result;
    return {true, "copied → " + rel_string(workdir, to)};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_move_file(const std::filesystem::path& workdir, const std::string& src, const std::string& dest) {
  try {
    const auto from = resolve_under(workdir, src);
    const auto to = resolve_under(workdir, dest);
    guard_private_state(from, true); guard_private_state(to, true);
    if(!std::filesystem::is_regular_file(from))return {false,"source not a regular file"};
    const auto content=tools_internal::read_file_bytes(from);const bool exists=std::filesystem::exists(to);
    if(exists&&!std::filesystem::is_regular_file(to))return {false,"destination is not a regular file"};
    const auto before=exists?tools_internal::read_file_bytes(to):std::string();
    if (exists) {const auto backup=backup_regular_file(workdir,to,&before);if(!backup.ok)return backup;}
    const auto source_backup=backup_regular_file(workdir,from,&content);if(!source_backup.ok)return source_backup;
    std::filesystem::create_directories(to.parent_path());
    auto destination=tools_internal::prepare_mutation(to,content,exists,exists?&before:nullptr);
    auto source=tools_internal::prepare_mutation(from,{},true,&content,false);bool moved=false;
    ToolResult result{true,{}};
    try{
      tools_internal::FileFd from_dir(tools_internal::open_parent(from)),to_dir(tools_internal::open_parent(to));
      tools_internal::FileFd from_fd(openat(from_dir.value,from.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
      struct stat source_stat{},current{};if(from_fd.value<0||fstat(from_fd.value,&source_stat)||tools_internal::read_fd(from_fd.value)!=content)
        throw std::runtime_error("FILE_CHANGED: source changed before move");
      if(exists){tools_internal::FileFd dest_fd(openat(to_dir.value,to.filename().c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
        if(dest_fd.value<0||tools_internal::read_fd(dest_fd.value)!=before)throw std::runtime_error("FILE_CHANGED: destination changed before move");}
      else if(fstatat(to_dir.value,to.filename().c_str(),&current,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT)
        throw std::runtime_error("FILE_CHANGED: destination appeared before move");
      if(renameat(from_dir.value,from.filename().c_str(),to_dir.value,to.filename().c_str()))
        throw std::runtime_error(std::string("move failed: ")+std::strerror(errno));
      moved=true;
      if(fsync(from_dir.value)||(from_dir.value!=to_dir.value&&fsync(to_dir.value)))result={false,"MOVED_UNSYNCED: directory sync failed"};
    }catch(const std::exception& e){result={false,e.what()};}
    result=tools_internal::finish_mutation(source,result,moved);
    result=tools_internal::finish_mutation(destination,result,moved);
    if(!result.ok)return result;
    return {true, "moved → " + rel_string(workdir, to)};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_delete_file(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto file = resolve_under(workdir, path);
    if (!std::filesystem::is_regular_file(file)) {
      return {false, "not a file: " + file.string()};
    }
    const auto backup = backup_regular_file(workdir, file);
    if (!backup.ok) return backup;
    const auto expected=tools_internal::read_file_bytes(file);
    const auto removed=remove_regular(file,expected);if(!removed.ok)return removed;
    return {true, "deleted " + path};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_delete_dir(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto dir = resolve_under(workdir, path);
    guard_private_state(dir, true);
    if (!std::filesystem::is_directory(dir)) {
      return {false, "not a directory: " + dir.string()};
    }
    const auto canon = std::filesystem::weakly_canonical(workdir);
    if (dir == canon) {
      return {false, "refusing to delete workdir root"};
    }
    std::vector<std::filesystem::path> files,dirs;std::uintmax_t bytes=0;
    for(auto it=std::filesystem::recursive_directory_iterator(dir);it!=std::filesystem::recursive_directory_iterator();++it){
      const auto status=it->symlink_status();
      if(std::filesystem::is_symlink(status)||(!std::filesystem::is_regular_file(status)&&!std::filesystem::is_directory(status)))
        return {false,"delete_dir: symlinks and special files are refused; tree unchanged"};
      if(std::filesystem::is_directory(status))dirs.push_back(it->path());
      else{files.push_back(it->path());bytes+=it->file_size();}
      if(files.size()+dirs.size()>1024||bytes>64*1024*1024)
        return {false,"delete_dir: tree exceeds 1024 entries or 64 MiB; tree unchanged"};
    }
    size_t removed=0;
    for(const auto& file:files){const auto body=tools_internal::read_file_bytes(file);
      const auto backup=backup_regular_file(workdir,file,&body);if(!backup.ok)return {false,"PARTIAL_DELETE after "+std::to_string(removed)+" entries: "+backup.output};
      const auto result=remove_regular(file,body);if(!result.ok)return {false,"PARTIAL_DELETE after "+std::to_string(removed)+" entries: "+result.output};++removed;}
    std::sort(dirs.begin(),dirs.end(),[](const auto& a,const auto& b){return a.native().size()>b.native().size();});
    for(const auto& nested:dirs){const auto result=remove_empty_dir(nested);if(!result.ok)return {false,"PARTIAL_DELETE after "+std::to_string(removed)+" entries: "+result.output};++removed;}
    const auto root_removed=remove_empty_dir(dir);if(!root_removed.ok)return {false,"PARTIAL_DELETE after "+std::to_string(removed)+" entries: "+root_removed.output};
    return {true, "removed " + std::to_string(removed+1) + " tracked entries under " + path};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_restore_file(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto file = resolve_under(workdir, path);
    const auto bak_file=tools_internal::backup_path(workdir,file);
    if(!std::filesystem::exists(bak_file))return {false,"no protected backup for this file; legacy .arli-bak is not trusted automatically"};
    tools_internal::prepare_backup_store(workdir,file,false);
    const auto content = tools_internal::read_file_bytes(bak_file);
    tools_internal::FileFd parent(tools_internal::open_parent(file, true));
    const bool exists = std::filesystem::exists(file);
    const auto before = exists ? tools_internal::read_file_bytes(file) : std::string();
    if (exists) {
      const auto backup = backup_regular_file(workdir, file, &before, false);
      if (!backup.ok) return backup;
    }
    const auto result = publish_file(file, content, exists, exists ? &before : nullptr, exists ? nullptr : &bak_file);
    if (!result.ok) return result;
    return {true, "restored from protected backup → " + path};
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

}  // namespace arli
