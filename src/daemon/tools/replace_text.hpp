#pragma once
#include "tools.hpp"

namespace arli {
// Unlike legacy edit_file, the strict tool does not coerce aliases or missing values.
inline std::string replace_text_args_error(const nlohmann::json& args) {
  for (const char* key : {"path", "old_text", "new_text"})
    if (!args.contains(key) || !args[key].is_string())
      return std::string("replace_text: требуется строка ") + key + "; файл не изменён";
  if (args["path"].get_ref<const std::string&>().empty()) return "replace_text: path пуст; файл не изменён";
  if (args["old_text"].get_ref<const std::string&>().empty())
    return "replace_text: old_text пуст; скопируй уникальный исходный фрагмент из read_file";
  return {};
}
// The expected snapshot is mandatory. Publication rechecks it after parsing.
ToolResult tool_replace_text(const std::filesystem::path& workdir, const std::string& path,
                             const std::string& old_text, const std::string& new_text,
                             const std::string& expected);
}
