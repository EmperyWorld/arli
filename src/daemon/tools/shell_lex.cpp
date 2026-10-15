#include "shell_lex.hpp"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace arli {
namespace {

enum class Q { None, S, D };

std::vector<std::string> split_segments(const std::string& cmd) {
  std::vector<std::string> out;
  std::string cur;
  Q q = Q::None;
  for (size_t i = 0; i < cmd.size(); ++i) {
    const char c = cmd[i];
    if (q == Q::None && c == '|' && (i + 1 >= cmd.size() || cmd[i + 1] != '|')) {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    if (q == Q::None && c == ';') {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    if (q == Q::None && c == '&' && i + 1 < cmd.size() && cmd[i + 1] == '&') {
      out.push_back(cur);
      cur.clear();
      ++i;
      continue;
    }
    if (q == Q::None && c == '|' && i + 1 < cmd.size() && cmd[i + 1] == '|') {
      out.push_back(cur);
      cur.clear();
      ++i;
      continue;
    }
    cur.push_back(c);
    if (q == Q::None && c == '\'') {
      q = Q::S;
    } else if (q == Q::S && c == '\'') {
      q = Q::None;
    } else if (q == Q::None && c == '"') {
      q = Q::D;
    } else if (q == Q::D && c == '"') {
      q = Q::None;
    }
  }
  if (!cur.empty() || out.empty()) {
    out.push_back(cur);
  }
  return out;
}

std::string first_word(const std::string& seg) {
  size_t i = 0;
  while (i < seg.size() && std::isspace(static_cast<unsigned char>(seg[i]))) {
    ++i;
  }
  while (i < seg.size() && seg[i] != '=') {
    // skip VAR= prefixes (simple)
    const size_t start = i;
    while (i < seg.size() && !std::isspace(static_cast<unsigned char>(seg[i])) && seg[i] != '=') {
      ++i;
    }
    if (i < seg.size() && seg[i] == '=') {
      i = start;
      while (i < seg.size() && seg[i] != '=') {
        ++i;
      }
      if (i < seg.size() && seg[i] == '=') {
        ++i;
      }
      while (i < seg.size() && std::isspace(static_cast<unsigned char>(seg[i]))) {
        ++i;
      }
      continue;
    }
    return seg.substr(start, i - start);
  }
  return {};
}

bool eq_ci(const std::string& a, const char* b) {
  if (a.size() != std::strlen(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool has_redirect(const std::string& seg) {
  Q q = Q::None;
  for (size_t i = 0; i < seg.size(); ++i) {
    const char c = seg[i];
    if (q == Q::None && c == '\'') {
      q = Q::S;
      continue;
    }
    if (q == Q::S && c == '\'') {
      q = Q::None;
      continue;
    }
    if (q == Q::None && c == '"') {
      q = Q::D;
      continue;
    }
    if (q == Q::D && c == '"') {
      q = Q::None;
      continue;
    }
    if (q != Q::None) {
      continue;
    }
    if (c == '<' && i + 1 < seg.size() && seg[i + 1] == '<') {
      return true;
    }
    if (c == '>') {
      if (i + 1 < seg.size() && seg[i + 1] == '>') {
        return true;
      }
      if (i > 0 && seg[i - 1] == '>') {
        continue;
      }
      if (i > 0 && (seg[i - 1] == '<' || seg[i - 1] == '=')) {
        continue;
      }
      return true;
    }
  }
  return false;
}

bool segment_mutates(const std::string& seg) {
  if (has_redirect(seg)) {
    return true;
  }
  const std::string low = [&] {
    std::string s = seg;
    for (char& c : s) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  }();
  if (low.find("| tee") != std::string::npos || low.find(" tee ") != std::string::npos) {
    return true;
  }
  const std::string w = first_word(seg);
  if (w.empty()) {
    return false;
  }
  static const char* kDeny[] = {"rm", "rmdir", "cp", "mv", "dd", "tee", "truncate", "chmod", "chown", "ln",
                                "install", "sqlite3"};
  for (const char* d : kDeny) {
    if (eq_ci(w, d)) {
      return true;
    }
  }
  if (eq_ci(w, "sed") && (low.find("-i") != std::string::npos || low.find("-i'") != std::string::npos)) {
    return true;
  }
  if (eq_ci(w, "perl") && low.find("-i") != std::string::npos) {
    return true;
  }
  if (eq_ci(w, "ruby") && low.find("-i") != std::string::npos) {
    return true;
  }
  return false;
}

}  // namespace

bool shell_mutates_files(const std::string& command) {
  if (command.empty()) {
    return false;
  }
  for (const auto& seg : split_segments(command)) {
    if (segment_mutates(seg)) {
      return true;
    }
  }
  return false;
}

}  // namespace arli
