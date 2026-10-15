#pragma once
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace arli::tools_internal {
struct FileFd {
  int value = -1;
  explicit FileFd(int fd = -1) : value(fd) {}
  ~FileFd() { if (value >= 0) close(value); }
  FileFd(const FileFd&) = delete;
  FileFd& operator=(const FileFd&) = delete;
};
inline int open_parent(const std::filesystem::path& file, bool create = false) {
  const auto absolute = std::filesystem::absolute(file).lexically_normal();
  FileFd dir(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  for (const auto& part : absolute.parent_path().relative_path()) {
    if (create) {
      const int made = mkdirat(dir.value, part.c_str(), 0700);
      if (made != 0 && errno != EEXIST) throw std::runtime_error("cannot create parent directory");
      if (made == 0 && fsync(dir.value)) throw std::runtime_error("cannot sync new parent directory");
    }
    const int next = openat(dir.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0) throw std::runtime_error("cannot open file parent without following symlinks");
    close(dir.value); dir.value = next;
  }
  const int result = dir.value; dir.value = -1; return result;
}
inline std::string read_fd(int fd) {
  struct stat before{}, after{};
  if (fstat(fd, &before) || !S_ISREG(before.st_mode)) throw std::runtime_error("not a regular file");
  constexpr size_t limit = 64 * 1024 * 1024;
  if (before.st_size > static_cast<off_t>(limit)) throw std::runtime_error("file exceeds 64 MiB mutation limit");
  std::string result;
  char buffer[16384];
  for (;;) {
    const auto n = pread(fd, buffer, sizeof(buffer), result.size());
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) throw std::runtime_error("file read failed");
    if (!n) break;
    if (result.size() + n > limit) throw std::runtime_error("file exceeds 64 MiB mutation limit");
    result.append(buffer, n);
  }
  if (fstat(fd, &after) || before.st_size != after.st_size ||
      before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
      before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
    throw std::runtime_error("FILE_CHANGED: source changed while reading");
  return result;
}
inline std::string read_file_bytes(const std::filesystem::path& file) {
  FileFd parent(open_parent(file));
  FileFd fd(openat(parent.value, file.filename().c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  if (fd.value < 0) throw std::runtime_error("cannot open regular file without following symlinks");
  return read_fd(fd.value);
}
}
