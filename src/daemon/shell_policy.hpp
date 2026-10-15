#pragma once

// Shell confirm policy. Patterns are token prefixes, one per line.
// `git` matches `git status`. `git status` does not match `git stash`.
// Ask-list wins over the allow-list. A command of several segments
// (&&, ||, |, ;) is automatic only when every segment is allowed and none is on the ask-list.
// Empty allow-list means: ask every time.

#include <cctype>
#include <string>
#include <vector>

namespace arli {
namespace shell_policy {

inline std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s;
}

inline std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  char quote = 0;
  for (char c : s) {
    if (quote) {
      if (c == quote) {
        quote = 0;
      } else {
        cur.push_back(c);
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
  return out;
}

inline std::vector<std::string> lines(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : text) {
    if (c == '\n' || c == '\r') {
      cur = trim(cur);
      if (!cur.empty() && cur.front() != '#') {
        out.push_back(cur);
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  cur = trim(cur);
  if (!cur.empty() && cur.front() != '#') {
    out.push_back(cur);
  }
  return out;
}

inline std::vector<std::string> segments(const std::string& command) {
  std::vector<std::string> out;
  std::string cur;
  char quote = 0;
  for (size_t i = 0; i < command.size(); ++i) {
    const char c = command[i];
    if (quote) {
      cur.push_back(c);
      if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      cur.push_back(c);
      continue;
    }
    const bool dbl_and = c == '&' && i + 1 < command.size() && command[i + 1] == '&';
    const bool dbl_or = c == '|' && i + 1 < command.size() && command[i + 1] == '|';
    if (dbl_and || dbl_or || c == '|' || c == ';') {
      out.push_back(cur);
      cur.clear();
      if (dbl_and || dbl_or) {
        ++i;
      }
      continue;
    }
    cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

inline bool prefix_match(const std::vector<std::string>& cmd, const std::string& pattern) {
  const auto pat = tokens(pattern);
  if (pat.empty() || cmd.size() < pat.size()) {
    return false;
  }
  for (size_t i = 0; i < pat.size(); ++i) {
    if (cmd[i] != pat[i]) {
      return false;
    }
  }
  return true;
}

inline bool any_match(const std::vector<std::string>& cmd, const std::string& rules) {
  for (const auto& line : lines(rules)) {
    if (prefix_match(cmd, line)) {
      return true;
    }
  }
  return false;
}

// True when the operator must be asked.
// allow_all means every command runs. Deletion is decided separately.
// An empty allow-list asks for every command when allow_all is false.
// The ask-list wins only while allow_all is off.
inline bool needs_confirm(const std::string& command, const std::string& allow, const std::string& ask,
                          bool allow_all = false) {
  if (allow_all) {
    return false;
  }
  const bool allow_empty = lines(allow).empty();
  for (const auto& seg : segments(command)) {
    const auto cmd = tokens(seg);
    if (cmd.empty()) {
      continue;
    }
    if (any_match(cmd, ask)) {
      return true;
    }
    if (allow_empty || !any_match(cmd, allow)) {
      return true;
    }
  }
  return false;
}

// A bare `&` (not `&&`) or a leading nohup: the process is meant to outlive the call.
inline bool is_background(const std::string& command) {
  const std::string s = trim(command);
  if (s.rfind("nohup ", 0) == 0 || s == "nohup") {
    return true;
  }
  char quote = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (quote) {
      if (c == quote) {
        quote = 0;
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c != '&') {
      continue;
    }
    const bool prev = i > 0 && s[i - 1] == '&';
    const bool next = i + 1 < s.size() && s[i + 1] == '&';
    if (!prev && !next) {
      return true;
    }
  }
  return false;
}

// Verification must reflect the tested program's exit status. These common
// suffixes turn a failed test into shell exit 0 and therefore cannot be used as
// task-stage or acceptance evidence.
inline bool masks_failure_status(const std::string& command) {
  const auto compact = trim(command);
  if (compact.find("$?") != std::string::npos) return true;
  const auto parts = segments(compact);
  if (parts.size() < 2) return false;
  const auto last = tokens(parts.back());
  if (last.empty()) return false;
  if (last[0] == "true" || last[0] == ":") return true;
  if ((last[0] == "echo" || last[0] == "printf") && compact.find(';') != std::string::npos) return true;
  const auto pos = compact.rfind("||");
  if (pos != std::string::npos) {
    const auto fallback = tokens(compact.substr(pos + 2));
    if (!fallback.empty() && (fallback[0] == "true" || fallback[0] == ":")) return true;
  }
  return false;
}

// rm / rmdir / unlink / find -delete / git clean. File writes via `>` stay a hard reject.
inline bool is_delete_command(const std::string& command) {
  for (const auto& seg : segments(command)) {
    const auto cmd = tokens(seg);
    if (cmd.empty()) {
      continue;
    }
    const std::string& bin = cmd[0];
    if (bin == "rm" || bin == "rmdir" || bin == "unlink") {
      return true;
    }
    if (bin == "git" && cmd.size() >= 2 && cmd[1] == "clean") {
      return true;
    }
    if (bin == "find") {
      for (const auto& tok : cmd) {
        if (tok == "-delete") {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace shell_policy
}  // namespace arli
