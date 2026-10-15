#include "create_policy.hpp"

#include <regex>

namespace arli {
namespace {
std::string safe_path(const std::string& value) {
  const std::filesystem::path path(value);
  if (value.empty() || path.is_absolute()) return {};
  for (const auto& part : path) if (part == "..") return {};
  const auto normalized = path.lexically_normal().generic_string();
  return normalized == "." ? std::string{} : normalized;
}
}  // namespace

void classify_creation_policy(TurnIntent& intent, const std::string& original, const std::string& lower) {
  std::string remainder = lower;
  bool scoped = false;
  for (const auto* phrase : {"других файлов не создавай", "не создавай других файлов",
                             "не создавать другие файлы", "не создавай остальные файлы",
                             "do not create other files", "don't create other files", "no other files"}) {
    for (auto pos = remainder.find(phrase); pos != std::string::npos; pos = remainder.find(phrase)) {
      remainder.replace(pos, std::string(phrase).size(), std::string(std::string(phrase).size(), ' '));
      scoped = true;
    }
  }
  bool global = false;
  for (const auto* phrase : {"не создавать", "не создавай", "новые файлы запрещены", "без новых файлов",
                             "create_file запрещ", "do not create files", "don't create files", "no new files"})
    global |= remainder.find(phrase) != std::string::npos;
  intent.forbid_create = global || scoped;
  if (!scoped || global || intent.forbid_mutation) return;

  // Only imperative commands at a sentence boundary. Paths retain original case.
  // Quoted paths may contain spaces; bare paths require an extension or slash.
  static const std::regex command(
      R"re((^|[.!?;\n][ \t\r\n]+)(создай|создать|create)[ \t]+(файл[ \t]+|file[ \t]+)?("([^"\n]+)"|'([^'\n]+)'|`([^`\n]+)`|([^ \t\r\n,;!?]+)))re");
  for (std::sregex_iterator it(lower.begin(), lower.end(), command), end; it != end; ++it) {
    const auto& m = *it;
    int group = m[5].matched ? 5 : m[6].matched ? 6 : m[7].matched ? 7 : 8;
    std::string value = original.substr(m.position(group), m.length(group));
    if (group == 8) {
      while (!value.empty() && value.back() == '.') value.pop_back();
      if (!m[3].matched && value.find_first_of("./") == std::string::npos) continue;
    }
    const auto path = safe_path(value);
    if (!path.empty()) intent.allowed_create_paths.push_back(path);
  }
}

ToolResult creation_gate(const TurnIntent& intent, const std::filesystem::path& workdir,
                         const std::string& tool, const std::string& args_json,bool readonly_shell) {
  if (!intent.forbid_create) return {true, {}};
  const ToolResult denied{false, "CREATE_FORBIDDEN: создание этого пути не разрешено пользователем. "
                                "Используй явно запрошенный путь; существующие файлы читай и редактируй."};
  // Arbitrary programs cannot enforce a per-path creation policy.
  if (tool == "run_shell" || tool == "start_background") return readonly_shell?ToolResult{true,{}}:denied;
  if (tool != "create_file" && tool != "write_file" && tool != "copy_file" &&
      tool != "move_file" && tool != "restore_file" && tool != "make_dir") return {true, {}};
  nlohmann::json args; std::string error;
  if (!parse_tool_args(args_json, args, error)) return {false, error};
  std::string value = coerce_path_arg(args);
  if (tool == "copy_file" || tool == "move_file") {
    value.clear();
    for (const auto* key : {"dest", "destination", "to"})
      if (args.contains(key) && args[key].is_string()) { value = args[key].get<std::string>(); break; }
  }
  const auto path = safe_path(value);
  if (path.empty()) return denied;
  if (tool == "move_file") {
    std::string source;
    for (const auto* key : {"src", "source", "from"})
      if (args.contains(key) && args[key].is_string()) { source = args[key].get<std::string>(); break; }
    // rename() also accepts directories: their descendants are not authorized by one file exception.
    if (safe_path(source).empty() || std::filesystem::is_symlink(workdir / source) ||
        !std::filesystem::is_regular_file(workdir / source)) return denied;
  }
  // Do not grant an exception through a symlink, even when its target is inside workdir.
  auto current = std::filesystem::weakly_canonical(workdir);
  for (const auto& part : std::filesystem::path(path)) {
    current /= part;
    std::error_code ec;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(current, ec))) return denied;
  }
  if (tool != "create_file" && std::filesystem::exists(current)) return {true, {}};
  for (const auto& allowed : intent.allowed_create_paths) {
    if (tool != "make_dir" && path == allowed) return {true, {}};
    if (tool == "make_dir" && allowed.rfind(path + "/", 0) == 0) return {true, {}};
  }
  return denied;
}
}  // namespace arli
