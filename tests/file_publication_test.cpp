#include "tools/atomic_publish.hpp"
#include "tools/backup.hpp"
#include "tools/backup_store.hpp"
#include "tools/tier_a.hpp"
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
namespace fs = std::filesystem;
enum Fault { None, Write, Sync, DirectorySync, Open, ExternalWrite, ParentSwap };
Fault fault = None;
fs::path external, swap_parent, swap_target;
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" int __real_fsync(int);
extern "C" int __real_openat(int, const char*, int, ...);
extern "C" ssize_t __wrap_write(int fd, const void* bytes, size_t size) {
  if (fault == Write) { errno = ENOSPC; return -1; }
  if (fault == ParentSwap) {
    fault = None;
    fs::rename(swap_parent, swap_parent.string() + "-parked");
    fs::create_directory_symlink(swap_target, swap_parent);
  }
  if (fault == ExternalWrite) {
    fault = None;
    const int other = open(external.c_str(), O_WRONLY | O_TRUNC);
    __real_write(other, "foreign", 7); close(other);
  }
  return __real_write(fd, bytes, size);
}
extern "C" int __wrap_fsync(int fd) {
  struct stat st{}; fstat(fd, &st);
  if (fault == Sync || (fault == DirectorySync && S_ISDIR(st.st_mode))) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
extern "C" int __wrap_openat(int dir, const char* name, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
  if (fault == Open) { errno = EACCES; return -1; }
  return __real_openat(dir, name, flags, mode);
}
std::string read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
void seed(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  char tmp[] = "/tmp/arli-publish-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), wd = root / "work", outside = root / "outside";
  fs::create_directory(wd); fs::create_directory(outside);
  const auto state=root/"state";fs::create_directory(state);chmod(state.c_str(),0700);setenv("ARLI_STATE_DIR",state.c_str(),1);
  const auto sibling = root / "work-neighbor"; fs::create_directory(sibling); seed(sibling / "foreign.txt", "foreign");
  CHECK(!arli::tool_create_file(wd, "../work-neighbor/new.txt", "bad").ok);
  CHECK(!fs::exists(sibling / "new.txt"));
  fs::create_directory_symlink(sibling, wd / "escape");
  CHECK(!arli::tool_read_file(wd, "../work-neighbor/foreign.txt").ok);
  CHECK(!arli::tool_read_file(wd, "escape/foreign.txt").ok);
  CHECK(!arli::tool_copy_file(wd, "escape/foreign.txt", "copied-secret.txt").ok);
  CHECK(!fs::exists(wd / "copied-secret.txt"));
  const auto file = wd / "file.txt"; seed(file, "original"); seed(wd / "source.txt", "copied");
  for (auto failure : {Write, Sync, Open}) {
    fault = failure; const auto result = arli::publish_file(file, "replacement", true); fault = None;
    CHECK(!result.ok && read(file) == "original");
    for (const auto& entry : fs::directory_iterator(wd)) CHECK(entry.path().filename().string().rfind(".arli-", 0) != 0);
  }
  std::string stale = "stale";
  CHECK(!arli::publish_file(file, "replacement", true, &stale).ok && read(file) == "original");
  external = file; fault = ExternalWrite;
  CHECK(!arli::publish_file(file, "replacement", true).ok && read(file) == "foreign");
  seed(file, "original");
  fs::create_directory_symlink(outside, state / "backups");
  CHECK(!arli::tool_edit_lines(wd, "file.txt", 1, 1, "replacement").ok);
  CHECK(read(file) == "original" && fs::is_empty(outside));
  fs::remove(state / "backups");
  for (int operation = 0; operation < 4; ++operation) {
    fault = Write;
    const auto result = operation == 0 ? arli::tool_copy_file(wd, "source.txt", "file.txt") :
        operation == 1 ? arli::tool_move_file(wd, "source.txt", "file.txt") :
        operation == 2 ? arli::tool_delete_file(wd, "file.txt") : arli::tool_edit_lines(wd, "file.txt", 1, 1, "new");
    fault = None;
    CHECK(!result.ok && read(file) == "original" && read(wd / "source.txt") == "copied");
  }
  CHECK(arli::tool_copy_file(wd, "source.txt", "file.txt").ok);
  CHECK(read(file) == "copied" && read(arli::tools_internal::backup_path(wd,file)) == "original");
  fault = Write; const auto restore = arli::tool_restore_file(wd, "file.txt"); fault = None;
  CHECK(!restore.ok && read(file) == "copied");
  CHECK(arli::tool_restore_file(wd, "file.txt").ok && read(file) == "original");
  CHECK(read(arli::tools_internal::backup_path(wd,file)) == "original");
  bool prior_found = false;
  for (const auto& item : fs::directory_iterator(arli::tools_internal::backup_path(wd,file).parent_path()))
    prior_found |= item.path().filename().string().find(".history-") != std::string::npos && read(item.path()) == "copied";
  CHECK(prior_found);
  fs::create_symlink(file, wd / "link");
  CHECK(!arli::publish_file(wd / "link", "bad", true).ok && read(file) == "original");
  fs::create_directory_symlink(outside, wd / "dirlink");
  CHECK(!arli::publish_file(wd / "dirlink/escape", "bad", false).ok && fs::is_empty(outside));
  fs::create_directory(wd / "swapped"); seed(wd / "swapped/file.txt", "inside"); seed(outside / "file.txt", "outside");
  swap_parent = wd / "swapped"; swap_target = outside; fault = ParentSwap;
  CHECK(!arli::publish_file(wd / "swapped/file.txt", "wrong", true).ok);
  CHECK(read(outside / "file.txt") == "outside" && read(wd / "swapped-parked/file.txt") == "inside");
  CHECK(setxattr(file.c_str(), "user.arli-test", "attribute", 9, 0) == 0);
  CHECK(arli::publish_file(file, "metadata", true).ok);
  char attribute[16]{};
  CHECK(getxattr(file.c_str(), "user.arli-test", attribute, sizeof(attribute)) == 9 && std::string(attribute, 9) == "attribute");
  chmod(file.c_str(), 0750);
  CHECK(arli::tool_delete_file(wd, "file.txt").ok && !fs::exists(file));
  CHECK(arli::tool_restore_file(wd, "file.txt").ok && read(file) == "metadata");
  struct stat restored{}; CHECK(stat(file.c_str(), &restored) == 0 && (restored.st_mode & 0777) == 0750);
  CHECK(getxattr(file.c_str(), "user.arli-test", attribute, sizeof(attribute)) == 9);
  fault = DirectorySync; const auto uncertain = arli::publish_file(file, "published", true); fault = None;
  CHECK(!uncertain.ok && uncertain.output.find("PUBLISHED_UNSYNCED") != std::string::npos && read(file) == "published");
  fs::remove_all(root);
}
