#include "file_pass.hpp"

#include "turn_policy.hpp"

#include <algorithm>
#include <set>

namespace arli {
namespace {

bool done_has(const std::vector<std::string>& done, const std::string& path) {
  for (const auto& d : done) {
    if (normalize_pass_path(d) == path) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::string normalize_pass_path(const std::string& path) {
  std::string p = path;
  while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) {
    p.erase(p.begin());
  }
  while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) {
    p.pop_back();
  }
  if (p.size() >= 2 && p[0] == '.' && (p[1] == '/' || p[1] == '\\')) {
    p = p.substr(2);
  }
  for (char& c : p) {
    if (c == '\\') {
      c = '/';
    }
  }
  return p;
}

FilePassState FilePassState::from_row(const FilePassRow& row) {
  FilePassState s;
  s.paths_ = row.paths;
  s.done_ = row.done;
  s.reviewed_ = row.reviewed;
  return s;
}

std::string FilePassState::head() const {
  for (const auto& p : paths_) {
    const std::string n = normalize_pass_path(p);
    if (!n.empty() && !done_has(done_, n) && !done_has(reviewed_, n)) {
      return n;
    }
  }
  return {};
}

bool FilePassState::is_done(const std::string& path) const {
  return done_has(done_, normalize_pass_path(path)) || done_has(reviewed_, normalize_pass_path(path));
}

bool FilePassState::paths_equal(const std::string& a, const std::string& b) const {
  return normalize_pass_path(a) == normalize_pass_path(b);
}

ToolResult FilePassState::set_paths(const nlohmann::json& args) {
  if (hold_) {
    return {false, "file_pass: сначала заверши текущий файл правкой или file_pass_review — очередь нельзя менять"};
  }
  if (!args.contains("paths") || !args["paths"].is_array()) {
    return {false, "file_pass_set: нужен paths[]"};
  }
  std::vector<std::string> next;
  std::set<std::string> seen;
  for (const auto& item : args["paths"]) {
    if (!item.is_string()) {
      continue;
    }
    const std::string n = normalize_pass_path(item.get<std::string>());
    if (n.empty() || seen.count(n)) {
      continue;
    }
    seen.insert(n);
    next.push_back(n);
    if (next.size() >= 200) {
      break;
    }
  }
  if (next.empty()) {
    return {false, "file_pass_set: paths пуст"};
  }
  paths_ = std::move(next);
  done_.clear();
  reviewed_.clear();
  read_.clear();
  hold_ = false;
  std::string out = "pass " + std::to_string(paths_.size()) + " файлов: ";
  for (size_t i = 0; i < paths_.size() && i < 8; ++i) {
    if (i) {
      out += ", ";
    }
    out += paths_[i];
  }
  if (paths_.size() > 8) {
    out += ", …";
  }
  return {true, out};
}

ToolResult FilePassState::clear() {
  paths_.clear();
  done_.clear();
  reviewed_.clear();
  read_.clear();
  hold_ = false;
  return {true, "file_pass сброшен"};
}

void FilePassState::on_read_ok(const std::string& path) {
  const std::string n = normalize_pass_path(path);
  if (!done_has(read_, n)) read_.push_back(n);
  const std::string h = head();
  if (!h.empty() && n == h) {
    hold_ = true;
  }
}

void FilePassState::on_mutation_ok(const std::string& path) {
  const std::string n = normalize_pass_path(path);
  bool queued = false;
  for (const auto& p : paths_) if (normalize_pass_path(p) == n) queued = true;
  if (!queued) return;
  if (!done_has(done_, n)) done_.push_back(n);
  reviewed_.erase(std::remove_if(reviewed_.begin(), reviewed_.end(),
      [&](const auto& p) { return normalize_pass_path(p) == n; }), reviewed_.end());
  hold_ = !head().empty() && done_has(read_, head());
}

ToolResult FilePassState::review_without_changes(const nlohmann::json& args) {
  const std::string path = normalize_pass_path(coerce_path_arg(args));
  const std::string note = args.value("note", "");
  if (path.empty() || path != head()) return {false, "file_pass_review: нужен path текущего файла очереди"};
  if (!done_has(read_, path)) return {false, "file_pass_review: сначала прочитай файл"};
  if (note.find_first_not_of(" \t\r\n") == std::string::npos)
    return {false, "file_pass_review: укажи причину, почему правка не нужна (note)"};
  reviewed_.push_back(path);
  hold_ = !head().empty() && done_has(read_, head());
  return {true, "проверен без правок: " + path + " — " + note};
}

void FilePassState::fill_snapshot(v1::FilePassSnapshot* out) const {
  out->clear_paths();
  out->clear_done();
  for (const auto& p : paths_) {
    out->add_paths(p);
  }
  for (const auto& d : done_) {
    out->add_done(d);
  }
  out->clear_reviewed();
  for (const auto& p : reviewed_) out->add_reviewed(p);
  out->set_hold_for_write(hold_);
}

void fill_turn_event_file_pass(const FilePassState& pass, v1::TurnEvent& ev) {
  pass.fill_snapshot(ev.mutable_file_pass());
}

ToolResult file_pass_gate_tool(const FilePassState& pass, const std::string& tool, const std::string&) {
  if (!pass.active() || !pass.hold_for_write()) {
    return {true, {}};
  }
  const std::string h = pass.head();
  if (tool == "file_pass_set") {
    return {false, "file_pass: заверши текущий файл правкой или file_pass_review (" + h + ")"};
  }
  return {true, {}};
}

}  // namespace arli
