#include "edit_stats.hpp"

#include <nlohmann/json.hpp>

namespace arli {
namespace {

int count_lines(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  int n = 1;
  for (char c : text) {
    if (c == '\n') {
      ++n;
    }
  }
  return n - (text.back() == '\n' ? 1 : 0);
}

std::string text_arg(const nlohmann::json& args, std::initializer_list<const char*> keys) {
  for (const auto key : keys) if (args.contains(key) && args[key].is_string()) return args[key].get<std::string>();
  return {};
}

std::string path_from_args(const nlohmann::json& j) {
  for (const char* k : {"path", "file", "file_path"}) {
    if (j.contains(k) && j[k].is_string()) {
      return j[k].get<std::string>();
    }
  }
  return {};
}

}  // namespace

void note_mutation_tool(TurnEditStats& stats, const std::string& tool, const std::string& args_json) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(args_json.empty() ? "{}" : args_json);
  } catch (...) {
    return;
  }
  const std::string path = path_from_args(j);
  if (!path.empty()) {
    stats.files.insert(path);
  }
  if (tool == "create_file" || tool == "append_file" || tool == "write_file") {
    stats.lines_added += count_lines(text_arg(j, {"content", "text", "body", "data"}));
    return;
  }
  if (tool == "replace_text") {
    stats.lines_removed += count_lines(text_arg(j, {"old_text"}));
    stats.lines_added += count_lines(text_arg(j, {"new_text"}));
    return;
  }
  if (tool == "batch_replace" && j.contains("edits") && j["edits"].is_array()) {
    for (const auto& edit : j["edits"]) {
      const auto batch_path = path_from_args(edit);
      if (!batch_path.empty()) stats.files.insert(batch_path);
      stats.lines_removed += count_lines(text_arg(edit, {"old_text"}));
      stats.lines_added += count_lines(text_arg(edit, {"new_text"}));
    }
    return;
  }
  if (tool == "edit_file") {
    if (j.contains("start_line") && j.contains("end_line")) {
      const int a = j.value("start_line", 0);
      const int b = j.value("end_line", 0);
      if (a > 0 && b >= a) {
        stats.lines_removed += b - a + 1;
      }
    } else stats.lines_removed += count_lines(text_arg(j, {"search", "old_text", "old", "oldText", "old_string", "oldString"}));
    stats.lines_added += count_lines(text_arg(j, {"text", "new_text", "new", "newText", "new_string", "newString", "replace"}));
    return;
  }
  if (tool == "delete_file") {
    stats.lines_removed += 1;
  }
}

void fill_done_edit_stats(const TurnEditStats& stats, v1::Done* done) {
  if (!done || stats.empty()) {
    return;
  }
  auto* e = done->mutable_edit_stats();
  e->set_lines_added(stats.lines_added);
  e->set_lines_removed(stats.lines_removed);
  e->set_files_touched(stats.files_touched());
}

}  // namespace arli
