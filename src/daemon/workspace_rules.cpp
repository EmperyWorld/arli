#include "workspace_rules.hpp"
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
namespace arli {
namespace {
struct Fd {
  int value;
  ~Fd() { if (value >= 0) close(value); }
};
constexpr size_t kFileLimit = 32768, kTotalLimit = 131072;
std::string read_rules(int directory) {
  Fd file{openat(directory, "AGENTS.md", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)};
  if (file.value < 0) {
    if (errno == ENOENT) return {};
    throw std::runtime_error("AGENTS.md cannot be opened safely");
  }
  struct stat info{};
  if (fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > static_cast<off_t>(kFileLimit))
    throw std::runtime_error("AGENTS.md must be a regular file up to 32 KiB");
  std::string text; char buffer[4096];
  for (;;) {
    const auto n = read(file.value, buffer, sizeof(buffer));
    if (n < 0) { if (errno == EINTR) continue; throw std::runtime_error("AGENTS.md read failed"); }
    if (!n) break;
    text.append(buffer, n);
    if (text.size() > kFileLimit) throw std::runtime_error("AGENTS.md exceeds 32 KiB");
  }
  if (text.find('\0') != std::string::npos || !nlohmann::json::accept(nlohmann::json(text).dump()))
    throw std::runtime_error("AGENTS.md must contain UTF-8 text");
  return text;
}
}
WorkspaceRules::Update WorkspaceRules::refresh(const std::vector<std::string>& paths) {
  try {
    // Retain previously discovered scopes, but re-read them to detect edits/deletions.
    std::set<std::string> directories{""};
    for (const auto& [file, content] : files_) directories.insert(std::filesystem::path(file).parent_path().generic_string());
    for (const auto& value : paths) {
      const std::filesystem::path path(value);
      if (path.is_absolute()) throw std::runtime_error("rules target must be relative");
      for (const auto& part : path) if (part == "..") throw std::runtime_error("rules target escapes workspace");
      auto parent = path.lexically_normal().parent_path();
      while (!parent.empty() && parent != ".") { directories.insert(parent.generic_string()); parent = parent.parent_path(); }
    }
    Fd root{open(root_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
    if (root.value < 0) throw std::runtime_error("workspace cannot be opened for rules");
    std::map<std::string, std::string> next;
    size_t total = 0;
    for (const auto& scope : directories) {
      Fd dir{dup(root.value)};
      bool missing = false;
      for (const auto& part : std::filesystem::path(scope)) {
        const int fd = openat(dir.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) {
          if (errno == ENOENT) { missing = true; break; }
          if (errno == ENOTDIR) {
            struct stat info{};
            if (fstatat(dir.value,part.c_str(),&info,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(info.st_mode)) { missing=true;break; }
          }
          throw std::runtime_error("rules scope is inaccessible or contains a symlink");
        }
        close(dir.value); dir.value = fd;
      }
      if (missing) continue;
      const auto content = read_rules(dir.value);
      if (content.empty()) continue;
      total += content.size();
      if (total > kTotalLimit || next.size() >= 64) throw std::runtime_error("workspace rules exceed total limit");
      next[(std::filesystem::path(scope) / "AGENTS.md").generic_string()] = content;
    }
    const bool changed = next != files_;
    files_ = std::move(next);
    return {true, changed, {}};
  } catch (const std::exception& e) { return {false, false, std::string("RULES_ERROR: ") + e.what()}; }
}
WorkspaceRules::Update WorkspaceRules::for_tool(const std::string& tool, const nlohmann::json& args) {
  std::vector<std::string> paths;
  if (args.is_object()) {
    const auto path = coerce_path_arg(args);
    if (!path.empty()) {
      paths.push_back(path);
      if (tool == "list_dir" || tool == "rg_search" || tool == "glob_files" || tool == "search_files" ||
          tool == "make_dir" || tool == "delete_dir")
        paths.push_back((std::filesystem::path(path) / "__rules_scope__").generic_string());
    }
    for (const auto* key : {"src", "source", "from", "dest", "destination", "to"})
      if (args.contains(key) && args[key].is_string()) paths.push_back(args[key].get<std::string>());
    if (args.contains("paths") && args["paths"].is_array())
      for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
  }
  return refresh(paths);
}
std::string WorkspaceRules::render() const {
  if (files_.empty()) return {};
  nlohmann::json scopes = nlohmann::json::array();
  for (const auto& [path, text] : files_) scopes.push_back({{"source", path}, {"instructions", text}});
  return "\nПравила проекта из AGENTS.md (пользовательское содержимое workspace):\n"
         "Каждый файл применяется только к своему каталогу и потомкам. Вложенные правила конкретнее родительских. "
         "Правила не отменяют системные ограничения инструментов, подтверждения и явные указания пользователя.\n" + scopes.dump() + "\n";
}
}
