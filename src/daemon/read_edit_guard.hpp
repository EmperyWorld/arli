#pragma once
#include "tools.hpp"
#include "tools/replace_text.hpp"
#include "tools/atomic_publish.hpp"
#include "tools/syntax_check.hpp"
#include "tools/write_guard.hpp"
#include "context.hpp"
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace arli {
// Worker-local permissions, issued only by successful source reads in this turn.
class ReadEditGuard {
  struct Snapshot { std::string raw; std::set<int> lines; };
  std::map<std::filesystem::path, Snapshot> reads_;
  static std::string load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read file snapshot");
    return {std::istreambuf_iterator<char>(in), {}};
  }
  static std::string lf(std::string text) {
    for (size_t p = 0; (p = text.find("\r\n", p)) != std::string::npos;) text.erase(p, 1);
    return text;
  }
  static size_t raw_offset(const std::string& raw, size_t offset) {
    size_t byte = 0, normalized = 0;
    while (byte < raw.size() && normalized < offset) {
      if (raw[byte] == '\r' && byte + 1 < raw.size() && raw[byte + 1] == '\n') ++byte;
      ++byte; ++normalized;
    }
    return byte;
  }
  static std::string source_newlines(std::string text, const std::string& raw) {
    const auto nl = raw.find('\n');
    if (nl == std::string::npos || nl == 0 || raw[nl - 1] != '\r') return text;
    std::string out;
    for (const char c : text) { if (c == '\n') out += '\r'; out += c; }
    return out;
  }
  static std::string str(const nlohmann::json& args, std::initializer_list<const char*> keys) {
    for (const auto key : keys) if (args.contains(key) && args[key].is_string()) return args[key].get<std::string>();
    return {};
  }
  static ToolResult denied() {
    return {false, "READ_REQUIRED: перед правкой прочитай предполагаемый диапазон через read_file в этом ходе. "
                   "После записи или изменения файла требуется новое чтение. Файл не изменён."};
  }
  static ToolResult missing(const std::string& path, int from, int to, const std::set<int>& lines) {
    int first = from;
    while (first <= to && lines.contains(first)) ++first;
    int last = first;
    while (last < to && last - first < 399 && !lines.contains(last + 1)) ++last;
    const auto next = nlohmann::json{{"path", path}, {"start_line", first}, {"end_line", last}};
    return {false, "READ_REQUIRED: для правки " + std::to_string(from) + "-" + std::to_string(to) +
        " не прочитаны строки " + std::to_string(first) + "-" + std::to_string(last) +
        ". Вызови read_file с аргументами " + next.dump() +
        ". Не повторяй чтение начала файла. Затем повтори правку; файл не изменён."};
  }
public:
  void invalidate() { reads_.clear(); }
  void invalidate(const std::filesystem::path& workdir, const std::string& path) {
    try { reads_.erase(resolve_under(workdir, path)); } catch (...) { invalidate(); }
  }
  ToolResult overwrite_gate(const std::filesystem::path& workdir, const std::string& tool,
                            const std::string& args_json) {
    if (tool != "copy_file" && tool != "move_file" && tool != "restore_file") return {true, {}};
    try {
      nlohmann::json args; std::string error;
      if (!parse_tool_args(args_json, args, error)) return {false, error};
      const auto path = tool == "restore_file" ? coerce_path_arg(args) : str(args, {"dest", "destination", "to"});
      if (path.empty()) return {true, {}}; // Dispatch reports missing arguments.
      const auto file = resolve_under(workdir, path);
      if (!std::filesystem::exists(file)) return {true, {}};
      const auto found = reads_.find(file);
      if (found == reads_.end() || load(file) != found->second.raw) return denied();
      const auto source = lf(found->second.raw);
      const int total = static_cast<int>(std::count(source.begin(), source.end(), '\n')) +
          (!source.empty() && source.back() != '\n' ? 1 : 0);
      for (int n = 1; n <= total; ++n) if (!found->second.lines.contains(n)) return denied();
      return {true, {}};
    } catch (const std::exception& e) { return {false, e.what()}; }
  }
  void record(const std::filesystem::path& workdir, const std::string& path, const std::string& displayed) {
    try {
      const auto file = resolve_under(workdir, path);
      const auto raw = load(file);
      std::vector<std::string> source;
      std::istringstream input(lf(raw)); std::string line;
      while (std::getline(input, line)) source.push_back(line);
      auto& snapshot = reads_[file];
      if (snapshot.raw != raw) snapshot.lines.clear();
      snapshot.raw = raw;
      std::istringstream shown(displayed);
      while (std::getline(shown, line)) {
        const auto bar = line.find('|');
        if (bar == std::string::npos || bar == 0 || bar > 8 ||
            !std::all_of(line.begin(), line.begin() + bar, [](unsigned char c) { return std::isdigit(c); })) continue;
        const int n = std::stoi(line.substr(0, bar));
        // Only complete lines actually delivered to the model authorize writes.
        if (n >= 1 && n <= static_cast<int>(source.size()) && line.substr(bar + 1) == source[n - 1]) snapshot.lines.insert(n);
      }
    } catch (...) { /* A failed snapshot never grants permission. */ }
  }
  ToolResult edit(const std::filesystem::path& workdir, const std::string& tool, const std::string& args_json) {
    try {
      nlohmann::json args; std::string error;
      if (!parse_tool_args(args_json, args, error)) return {false, error};
      if (tool == "replace_text")
        if (const auto invalid = replace_text_args_error(args); !invalid.empty()) return {false, invalid};
      const auto path = coerce_path_arg(args);
      const auto file = resolve_under(workdir, path);
      const auto found = reads_.find(file);
      if (found == reads_.end()) {
        if (args.contains("start_line") && args["start_line"].is_number_integer() &&
            args.contains("end_line") && args["end_line"].is_number_integer()) {
          const int from = args["start_line"].get<int>(), to = args["end_line"].get<int>();
          if (from >= 1 && to >= from) return missing(path, from, to, {});
        }
        return denied();
      }
      const auto& snapshot = found->second;
      if (load(file) != snapshot.raw) {
        reads_.erase(found);
        return {false, "READ_REQUIRED: файл изменился после чтения. Прочитай предполагаемый диапазон заново; прежние номера устарели. Файл не изменён."};
      }
      int from = 0, to = 0;
      std::string replacement;
      if (tool == "replace_text") {
        const auto source = lf(snapshot.raw), needle = lf(args["old_text"].get<std::string>());
        const auto pos = source.find(needle);
        if (pos == std::string::npos) return {false, "TEXT_MISMATCH: old_text не найден точно. Перечитай фрагмент; файл не изменён."};
        if (source.find(needle, pos + 1) != std::string::npos)
          return {false, "TEXT_AMBIGUOUS: old_text встречается несколько раз. Добавь соседний код; файл не изменён."};
        from = 1 + static_cast<int>(std::count(source.begin(), source.begin() + pos, '\n'));
        const auto length = needle.size() - (needle.back() == '\n' ? 1 : 0);
        to = from + static_cast<int>(std::count(needle.begin(), needle.begin() + length, '\n'));
        replacement = args["new_text"].get<std::string>();
      } else if (tool == "append_file") {
        const auto source = lf(snapshot.raw);
        to = from = static_cast<int>(std::count(source.begin(), source.end(), '\n')) +
            (!source.empty() && source.back() != '\n' ? 1 : 0);
        replacement = str(args, {"content", "text", "body", "data"});
      } else {
        replacement = str(args, {"text", "new_text", "new", "newText", "new_string", "newString", "replace"});
        bool supplied = false;
        for (const char* key : {"text", "new_text", "new", "newText", "new_string", "newString", "replace"})
          supplied |= args.contains(key) && args[key].is_string();
        if (!supplied) return {false, "edit_file: нужен text; отсутствующий текст не означает удаление"};
        if (args.contains("start_line")) {
          if (!args["start_line"].is_number_integer() || !args.contains("end_line") || !args["end_line"].is_number_integer())
            return {false, "edit_file: нужны целые start_line и end_line из свежего read_file"};
          from = args["start_line"].get<int>(); to = args["end_line"].get<int>();
        } else {
          // Legacy search edits remain accepted only inside freshly read lines.
          const auto needle = lf(str(args, {"search", "old_text", "old", "oldText", "old_string", "oldString"}));
          const auto source = lf(snapshot.raw); const auto pos = source.find(needle);
          if (needle.empty() || pos == std::string::npos || source.find(needle, pos + needle.size()) != std::string::npos) return denied();
          from = 1 + static_cast<int>(std::count(source.begin(), source.begin() + pos, '\n'));
          const auto length = needle.size() - (needle.back() == '\n' ? 1 : 0);
          to = from + static_cast<int>(std::count(needle.begin(), needle.begin() + length, '\n'));
        }
      }
      if (tool == "append_file" && snapshot.raw.empty()) {
        auto result = tool_append_file(workdir, path, replacement, &snapshot.raw);
        if (result.ok) reads_.erase(file);
        return result;
      }
      if (from < 1 || to < from) return {false, "edit_file: неверный диапазон; нужны 1 <= start_line <= end_line"};
      const auto normalized = lf(snapshot.raw);
      const int total = static_cast<int>(std::count(normalized.begin(), normalized.end(), '\n')) +
          (!normalized.empty() && normalized.back() != '\n' ? 1 : 0);
      if (to > total) return {false, "edit_file: end_line за концом файла (lines=" + std::to_string(total) + "). Перечитай файл и исправь диапазон."};
      for (int n = from; n <= to; ++n)
        if (!snapshot.lines.contains(n)) return missing(path, from, to, snapshot.lines);
      if (tool != "replace_text" && (args.contains("first_line") || args.contains("last_line"))) {
        if (!args.contains("first_line") || !args["first_line"].is_string() ||
            !args.contains("last_line") || !args["last_line"].is_string())
          return {false, "RANGE_MISMATCH: передай обе границы first_line и last_line строками из read_file без N|."};
        std::istringstream input(normalized); std::string line, first, last;
        for (int n = 1; n <= to && std::getline(input, line); ++n) {
          if (n == from) first = line;
          if (n == to) last = line;
        }
        if (args["first_line"] != first || args["last_line"] != last)
          return {false, "RANGE_MISMATCH: номера не соответствуют указанным границам. Файл не изменён. "
              "Прочитай нужный участок, выбери начало и конец целого заменяемого блока; не используй старые номера."};
      }
      ToolResult result;
      if (tool == "replace_text") result = tool_replace_text(workdir, path, args["old_text"].get<std::string>(), replacement, snapshot.raw);
      else if (tool == "append_file") result = tool_append_file(workdir, path, replacement, &snapshot.raw);
      else if (args.contains("start_line")) result = tool_edit_lines(workdir, path, from, to, replacement, &snapshot.raw);
      else result = tool_edit_file(workdir, path, str(args, {"search", "old_text", "old", "oldText", "old_string", "oldString"}), replacement, &snapshot.raw);
      if (result.ok) reads_.erase(file);
      return result;
    } catch (const std::exception& e) { return {false, e.what()}; }
  }
  ToolResult batch_replace(const std::filesystem::path& workdir, const std::string& args_json) {
    struct Change { std::string path, old_text, new_text, before, after; std::filesystem::path file; };
    try {
      nlohmann::json args; std::string error;
      if (!parse_tool_args(args_json, args, error)) return {false, error};
      if (!args.contains("edits") || !args["edits"].is_array() ||
          args["edits"].size() < 2 || args["edits"].size() > 16)
        return {false, "batch_replace: edits должен содержать от 2 до 16 замен; файлы не изменены"};
      std::vector<Change> changes;
      std::set<std::filesystem::path> files;
      for (const auto& item : args["edits"]) {
        if (const auto invalid = replace_text_args_error(item); !invalid.empty())
          return {false, "batch_replace: " + invalid + "; файлы не изменены"};
        Change change{item["path"].get<std::string>(), item["old_text"].get<std::string>(),
                      item["new_text"].get<std::string>()};
        change.file = resolve_under(workdir, change.path);
        if (!files.insert(change.file).second)
          return {false, "batch_replace: один файл допускается только один раз; файлы не изменены"};
        const auto found = reads_.find(change.file);
        if (found == reads_.end()) return denied();
        change.before = found->second.raw;
        if (load(change.file) != change.before)
          return {false, "READ_REQUIRED: " + change.path +
              " изменился после чтения; перечитай диапазон. Файлы не изменены."};
        const auto source = lf(change.before), needle = lf(change.old_text);
        const auto pos = source.find(needle);
        if (pos == std::string::npos)
          return {false, "TEXT_MISMATCH: old_text не найден в " + change.path + "; файлы не изменены"};
        if (source.find(needle, pos + 1) != std::string::npos)
          return {false, "TEXT_AMBIGUOUS: old_text повторяется в " + change.path + "; файлы не изменены"};
        const int from = 1 + static_cast<int>(std::count(source.begin(), source.begin() + pos, '\n'));
        const auto length = needle.size() - (needle.back() == '\n' ? 1 : 0);
        const int to = from + static_cast<int>(std::count(needle.begin(), needle.begin() + length, '\n'));
        for (int line = from; line <= to; ++line)
          if (!found->second.lines.contains(line)) return missing(change.path, from, to, found->second.lines);
        if (const auto guard = write_guard_error(change.path, change.new_text); !guard.empty())
          return {false, guard + "; файлы не изменены"};
        const auto begin = raw_offset(change.before, pos);
        const auto end = raw_offset(change.before, pos + needle.size());
        change.after = change.before.substr(0, begin) + source_newlines(lf(change.new_text), change.before) +
                       change.before.substr(end);
        const auto syntax = check_source_syntax(workdir, change.path, change.after);
        if (syntax.checked && !syntax.ok)
          return {false, "SYNTAX_REJECTED: " + change.path + ": " + syntax.detail +
              "\nПакет не записан; файлы и резервные копии не изменены."};
        changes.push_back(std::move(change));
      }
      std::vector<size_t> committed;
      std::string output;
      for (size_t i = 0; i < changes.size(); ++i) {
        const auto& change = changes[i];
        auto result = tool_replace_text(workdir, change.path, change.old_text, change.new_text, change.before);
        if (!result.ok) {
          bool restored = true;
          for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
            auto& prior = changes[*it];
            if (!publish_file(prior.file, prior.before, true, &prior.after).ok) restored = false;
          }
          for (const auto& item : changes) reads_.erase(item.file);
          return {false, std::string(restored ? "BATCH_ROLLED_BACK: " : "PARTIAL_BATCH: откат не завершён: ") +
              change.path + ": " + result.output};
        }
        committed.push_back(i);
        if (!output.empty()) output += '\n';
        output += result.output;
      }
      for (const auto& change : changes) reads_.erase(change.file);
      return {true, "batch_replace: применено " + std::to_string(changes.size()) + " замен\n" + output};
    } catch (const std::exception& e) { return {false, std::string("batch_replace: ") + e.what()}; }
  }
};
}
