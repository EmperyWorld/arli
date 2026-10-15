#include "backup.hpp"
#include "backup_store.hpp"
#include "atomic_publish.hpp"
#include "file_io.hpp"
#include <sys/random.h>
namespace arli::tools_internal {
ToolResult backup_regular_file(const std::filesystem::path& workdir, const std::filesystem::path& file,
                               const std::string* expected, bool update_latest) {
  try {
    const auto bytes = read_file_bytes(file);
    if (expected && bytes != *expected) return {false, "FILE_CHANGED: source changed before backup"};
    prepare_backup_store(workdir,file,true);
    const auto dest=backup_path(workdir,file);
    FileFd parent(open_parent(dest));
    unsigned char random[16];
    if (getrandom(random, sizeof(random), 0) != sizeof(random)) return {false, "BACKUP_FAILED: random name unavailable"};
    std::string suffix = ".history-";
    for (auto byte : random) { suffix += "0123456789abcdef"[byte >> 4]; suffix += "0123456789abcdef"[byte & 15]; }
    const auto version = std::filesystem::path(dest.string() + suffix);
    const auto archived = publish_file(version, bytes, false, nullptr, &file);
    if (!archived.ok) return {false, "BACKUP_FAILED: " + archived.output};
    if (read_file_bytes(version) != bytes) return {false, "BACKUP_FAILED: archive verification mismatch"};
    if (!update_latest) {
      if (read_file_bytes(file) != bytes) return {false, "FILE_CHANGED: source changed during backup"};
      return {true, version.string()};
    }
    struct stat st{};
    const bool exists = fstatat(parent.value, dest.filename().c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0;
    const auto saved = publish_file(dest, bytes, exists, nullptr, &file);
    if (!saved.ok) return {false, "BACKUP_FAILED: " + saved.output};
    if (read_file_bytes(dest) != bytes) return {false, "BACKUP_FAILED: verification mismatch"};
    if (read_file_bytes(file) != bytes) return {false, "FILE_CHANGED: source changed during backup"};
    return {true, version.string()};
  } catch (const std::exception& e) { return {false, "BACKUP_FAILED: " + std::string(e.what())}; }
}
}
