#pragma once
#include "verification_edits.hpp"
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace arli {
class VerificationState {
  struct Check { std::string command; bool passed = false; bool blocked = false; };
  std::map<std::string, Check> checks_;
  std::set<std::string> rechecks_;
  std::set<std::string> external_errors_;
  VerificationEdits edits_;
public:
  void external_result(const std::string& tool, bool ok) {
    if (ok) external_errors_.erase(tool); else external_errors_.insert(tool);
  }
  void file_result(const std::string& tool, const std::string& path, bool ok, const std::string& output,
                   const nlohmann::json& args = {}) {
    edits_.result(tool, path, ok, output, args);
  }
  bool abandon(const std::string& path, const std::string& reason) { return edits_.abandon(path, reason); }
  bool has_abandoned() const { return edits_.has_abandoned(); }
  bool incomplete() const { return !pending().empty() || has_abandoned(); }
  std::string abandoned_summary() const { return edits_.abandoned_summary(); }
  std::string serialized() const { return edits_.serialized(); }
  bool restore(const std::string& state) { return edits_.restore(state); }
  // Only an explicit successful stage deferral may retire those exact command
  // checks. Rejected file operations and independent checks remain unresolved.
  void defer_checks(const std::vector<std::string>& commands) {
    for (auto it = checks_.begin(); it != checks_.end();) {
      if (std::find(commands.begin(), commands.end(), it->second.command) != commands.end() ||
          std::find(commands.begin(), commands.end(), it->first) != commands.end()) {
        rechecks_.erase(it->first); it = checks_.erase(it);
      } else ++it;
    }
  }
  void mutation() {
    for (auto& [key, check] : checks_) {
      check.passed = false;
      if (!check.blocked) rechecks_.insert(key);
    }
  }
  std::string due() const { return rechecks_.empty() ? std::string() : checks_.at(*rechecks_.begin()).command; }
  void observe(const std::string& command, bool ok, const std::string& output) {
    std::istringstream input(command); std::vector<std::string> words; std::string word;
    while (input >> word) words.push_back(word);
    if (words.empty()) return;
    const auto exe = std::filesystem::path(words[0]).filename().string();
    auto has = [&](const char* flag) { for (const auto& w : words) if (w == flag) return true; return false; };
    bool test_script = false;
    for (size_t i = 1; i < words.size(); ++i) {
      const auto file = std::filesystem::path(words[i]);
      const auto name = file.filename().string(), ext = file.extension().string();
      test_script |= (ext == ".js" || ext == ".cjs" || ext == ".mjs") &&
          (name.find("test") != std::string::npos || name.find("check") != std::string::npos);
    }
    const bool check = (exe == "node" && (has("--check") || has("--test") || test_script)) ||
        ((exe == "sh" || exe == "bash") && has("-n")) || exe == "ctest" ||
        ((exe == "npm" || exe == "pnpm" || exe == "yarn") && (has("test") || has("build") || has("lint"))) ||
        ((exe == "python" || exe == "python3") && has("-m") && (has("pytest") || has("unittest"))) ||
        (exe == "cargo" && (has("test") || has("check") || has("build"))) ||
        (exe == "go" && (has("test") || has("build") || has("vet"))) || (exe == "cmake" && has("--build"));
    if (!check) return;
    std::string key = exe;
    for (size_t i = 1; i < words.size(); ++i) {
      auto token = words[i]; while (token.rfind("./", 0) == 0) token.erase(0, 2);
      key += " " + token;
    }
    const bool executed = ok || output.rfind("exit ", 0) == 0 || output.find("timeout") != std::string::npos;
    if (executed) checks_[key] = {command, ok, false};
    else checks_[key] = {command, false, true};
    rechecks_.erase(key);
  }
  std::string pending() const {
    if (!external_errors_.empty()) return "Не подтверждён gRPC вызов " + *external_errors_.begin();
    if (const auto edit = edits_.pending(); !edit.empty()) return edit;
    for (const auto& [key, check] : checks_) if (!check.passed) return check.command;
    return {};
  }
  bool blocked() const {
    if (!external_errors_.empty()) return true;
    for (const auto& [key, check] : checks_) if (!check.passed && check.blocked) return true;
    return false;
  }
};
}
