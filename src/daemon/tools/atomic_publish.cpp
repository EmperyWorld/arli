#include "atomic_publish.hpp"
#include "mutation_journal.hpp"
#include "file_io.hpp"
#include <cstring>
#include <vector>
#include <sys/random.h>
#include <sys/xattr.h>
namespace arli {
namespace {
using tools_internal::FileFd;
void metadata(int source, int target, const struct stat& original) {
  struct stat current{};
  if (fstat(target, &current)) throw std::runtime_error("cannot stat temporary file");
  if ((original.st_uid != current.st_uid || original.st_gid != current.st_gid) &&
      fchown(target, original.st_uid, original.st_gid)) throw std::runtime_error("cannot preserve file owner");
  if (fchmod(target, original.st_mode & 0777)) throw std::runtime_error("cannot preserve file permissions");
  const auto size = flistxattr(source, nullptr, 0);
  if (size < 0 && errno == ENOTSUP) return;
  if (size < 0) throw std::runtime_error("cannot list file attributes");
  std::vector<char> names(size);
  if (size && flistxattr(source, names.data(), names.size()) != size) throw std::runtime_error("file attributes changed");
  for (size_t offset = 0; offset < names.size();) {
    const char* name = names.data() + offset;
    offset += std::strlen(name) + 1;
    if (std::strcmp(name, "security.capability") == 0) continue;
    const auto length = fgetxattr(source, name, nullptr, 0);
    if (length < 0) throw std::runtime_error("cannot read file attribute");
    std::vector<char> value(length);
    if (fgetxattr(source, name, value.data(), value.size()) != length ||
        fsetxattr(target, name, value.data(), value.size(), 0)) throw std::runtime_error("cannot preserve file attribute");
  }
}
}
static ToolResult publish_raw(const std::filesystem::path& file, const std::string& content,
                        bool replace, const std::string* expected, const std::filesystem::path* metadata_source,bool& published) {
  try {
    FileFd dir(tools_internal::open_parent(file));
    const auto name = file.filename().string();
    FileFd original(replace ? openat(dir.value, name.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC) : -1);
    struct stat before{};
    if (replace && (original.value < 0 || fstat(original.value, &before) || !S_ISREG(before.st_mode)))
      return {false, "cannot open original regular file"};
    const std::string observed = replace ? tools_internal::read_fd(original.value) : std::string();
    if (expected && observed != *expected) return {false, "FILE_CHANGED: original differs from expected; reread before editing"};
    unsigned char random[16];
    if (getrandom(random, sizeof(random), 0) != sizeof(random)) return {false, "cannot allocate random temporary name"};
    std::string temp = ".arli-";
    for (auto byte : random) { temp += "0123456789abcdef"[byte >> 4]; temp += "0123456789abcdef"[byte & 15]; }
    FileFd fd(openat(dir.value, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) return {false, "cannot create exclusive temporary file"};
    struct Cleanup { int dir; std::string name; bool active = true; ~Cleanup() { if (active) unlinkat(dir, name.c_str(), 0); } } cleanup{dir.value, temp};
    for (size_t offset = 0; offset < content.size();) {
      const auto n = write(fd.value, content.data() + offset, content.size() - offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return {false, "temporary file write failed: " + std::string(std::strerror(errno))};
      offset += n;
    }
    if (metadata_source) {
      FileFd source_parent(tools_internal::open_parent(*metadata_source));
      FileFd source(openat(source_parent.value, metadata_source->filename().c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
      struct stat attributes{};
      if (source.value < 0 || fstat(source.value, &attributes) || tools_internal::read_fd(source.value) != content)
        return {false, "FILE_CHANGED: metadata source differs from copied content"};
      metadata(source.value, fd.value, attributes);
    } else if (replace) metadata(original.value, fd.value, before);
    if (fsync(fd.value)) return {false, "temporary file sync failed"};
    const int closed = close(fd.value); fd.value = -1;
    if (closed) return {false, "temporary file close failed"};
    if (replace) {
      struct stat current{};
      if (fstatat(dir.value, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) ||
          current.st_dev != before.st_dev || current.st_ino != before.st_ino ||
          current.st_ctim.tv_sec != before.st_ctim.tv_sec || current.st_ctim.tv_nsec != before.st_ctim.tv_nsec ||
          tools_internal::read_fd(original.value) != observed)
        return {false, "FILE_CHANGED: original changed before publication"};
    }
    FileFd current_parent(tools_internal::open_parent(file));
    struct stat anchored{}, resolved{};
    if (fstat(dir.value, &anchored) || fstat(current_parent.value, &resolved) ||
        anchored.st_dev != resolved.st_dev || anchored.st_ino != resolved.st_ino)
      return {false, "FILE_CHANGED: parent directory moved before publication"};
    const int rc = replace ? renameat(dir.value, temp.c_str(), dir.value, name.c_str())
                           : linkat(dir.value, temp.c_str(), dir.value, name.c_str(), 0);
    if (rc) return {false, "atomic publish failed: " + std::string(std::strerror(errno))};
    published=true;
    if (unlinkat(dir.value, temp.c_str(), 0) && errno != ENOENT)
      return {false, "PUBLISHED_UNSYNCED: file published but temporary name cleanup failed"};
    cleanup.active = false;
    if (fsync(dir.value)) return {false, "PUBLISHED_UNSYNCED: file published but directory sync failed"};
    return {true, {}};
  } catch (const std::exception& e) { return {false, e.what()}; }
}
ToolResult publish_file(const std::filesystem::path& file,const std::string& content,bool replace,
                        const std::string* expected,const std::filesystem::path* metadata_source){
  try{
    const auto ticket=tools_internal::prepare_mutation(file,content,replace,expected);
    const auto* checked=(ticket.state&&replace)?&ticket.before:expected;
    bool published=false;auto result=publish_raw(file,content,replace,checked,metadata_source,published);
    return tools_internal::finish_mutation(ticket,std::move(result),published);
  }catch(const std::exception& e){return {false,e.what()};}
}

}
