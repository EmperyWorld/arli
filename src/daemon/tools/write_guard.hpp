#pragma once
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace arli {
inline bool source_path(const std::string& path) {
  const auto ext = std::filesystem::path(path).extension().string();
  const std::string code = ".js .mjs .cjs .ts .tsx .jsx .cpp .hpp .c .h .py .qml .css .html .json .sh .rs .go .java ";
  return !ext.empty() && code.find(ext + " ") != std::string::npos;
}
// Reject read_file's display gutters copied into source code. Never silently strip user text.
inline bool copied_line_gutters(const std::string& path, std::string_view text) {
  if (!source_path(path)) return false;
  int previous = -2;
  for (size_t begin = 0; begin < text.size();) {
    size_t end = text.find('\n', begin);
    if (end == std::string_view::npos) end = text.size();
    const auto line = text.substr(begin, end - begin);
    size_t i = 0;
    int number = 0;
    while (i < line.size() && i < 8 && std::isdigit(static_cast<unsigned char>(line[i]))) {
      number = number * 10 + line[i++] - '0';
    }
    if (i > 0 && i < line.size() && line[i] == '|') {
      if (number == previous + 1) return true;
      previous = number;
    } else previous = -2;
    begin = end + 1;
  }
  return false;
}
inline std::string write_guard_error(const std::string& path, const std::string& text) {
  if (source_path(path) && text.find("<tool_call|><|channel>") != std::string::npos)
    return "text содержит служебную последовательность протокола LLM. Файл не изменён. Передай только исходный код.";
  return copied_line_gutters(path, text)
      ? "new_text содержит номера строк N| из read_file. Файл не изменён. Передай только исходный код, без префиксов."
      : std::string();
}
}  // namespace arli
