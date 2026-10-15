#include "context_summary.hpp"
#include "context.hpp"
#include "turn_policy.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace arli::context {
namespace {
using json = nlohmann::json;
constexpr const char* kPrefix = "Сжатый контекст (только подтверждённые результаты):\n";

std::string text(const json& value, const char* key) {
  return value.contains(key) && value[key].is_string() ? value[key].get<std::string>() : std::string();
}

void merge_set(std::set<std::string>& target, const json& value, const char* key) {
  if (!value.contains(key) || !value[key].is_array()) return;
  for (const auto& item : value[key]) if (item.is_string()) target.insert(item.get<std::string>());
}

void add_instruction(std::vector<std::string>& target, std::string value) {
  value = clip_text(std::move(value), 600);
  if (value.empty()) return;
  if (std::find(target.begin(), target.end(), value) == target.end()) target.push_back(std::move(value));
  if (target.size() > 4) target.erase(target.begin());
}

json bounded(const std::set<std::string>& values, int limit = 16) {
  json result = json::array();
  for (const auto& item : values) {
    if (static_cast<int>(result.size()) >= limit) break;
    result.push_back(clip_text(item, 240));
  }
  return result;
}
}  // namespace

std::string build_fold_summary(const json& messages, int from, int to) {
  std::set<std::string> changed, read, listed, shell, errors;
  std::vector<std::string> instructions;
  std::map<std::string, json> calls;
  for (int i = 0; i < to && i < static_cast<int>(messages.size()); ++i) {
    const auto& m = messages[i];
    const std::string role = text(m, "role");
    // Calls before 'from' can have their observations in the folded interval.
    if (role == "assistant" && m.contains("tool_calls") && m["tool_calls"].is_array()) {
      for (const auto& call : m["tool_calls"]) {
        if (call.contains("function") && call["function"].is_object()) calls[text(call, "id")] = call["function"];
      }
    }
    if (i < from) continue;
    const std::string content = text(m, "content");
    if (role == "user") {
      if (content.rfind(kPrefix, 0) == 0) {
        const auto previous = json::parse(content.substr(std::char_traits<char>::length(kPrefix)), nullptr, false);
        if (previous.is_object()) {
          merge_set(changed, previous, "Изменены");
          merge_set(read, previous, "Прочитаны");
          merge_set(listed, previous, "Обход");
          merge_set(shell, previous, "Shell выполнен");
          merge_set(errors, previous, "Ошибки");
          if (previous.contains("Указания пользователя") && previous["Указания пользователя"].is_array())
            for (const auto& item : previous["Указания пользователя"])
              if (item.is_string()) add_instruction(instructions, item.get<std::string>());
        }
      } else {
        add_instruction(instructions, content);
      }
      continue;
    }
    if (role != "tool") continue;
    const auto call = calls.find(text(m, "tool_call_id"));
    if (call == calls.end()) continue;
    const auto& fn = call->second;
    const std::string name = text(fn, "name");
    json args = fn.value("arguments", json::object());
    if (args.is_string()) args = json::parse(args.get<std::string>(), nullptr, false);
    if (!args.is_object()) continue;
    std::string path;
    for (const char* key : {"path", "file", "file_path", "filePath", "target", "destination", "dest"}) {
      path = text(args, key);
      if (!path.empty()) break;
    }
    if (content.rfind("ERROR\n", 0) == 0) {
      errors.insert(clip_text(name + " " + path + ": " + content.substr(6), 240));
      continue;
    }
    // Old tool messages had no explicit result flag: don't invent success.
    if (content.rfind("OK\n", 0) != 0) continue;
    if (tool_writes_workdir(name)) {
      if (!path.empty()) changed.insert(path);
    } else if (name == "read_file" || name == "quote_file" || name == "rg_search") {
      if (!path.empty()) read.insert(path);
    } else if (name == "list_dir" || name == "glob_files" || name == "search_files") {
      listed.insert(path.empty() ? "." : path);
    } else if (name == "run_shell" || name == "start_background") {
      shell.insert(clip_text(text(args, "command"), 120));
    }
  }
  const json summary = {{"Указания пользователя", instructions}, {"Изменены", bounded(changed)},
                        {"Прочитаны", bounded(read)}, {"Обход", bounded(listed)},
                        {"Shell выполнен", bounded(shell, 6)}, {"Ошибки", bounded(errors, 6)}};
  return std::string(kPrefix) + summary.dump(2);
}
}  // namespace arli::context
