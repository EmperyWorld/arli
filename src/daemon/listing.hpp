#pragma once

// Directories hidden from list_dir, rg and listing-style shell output.
// One name per line. Empty configuration uses the built-in set.

#include "shell_policy.hpp"

#include <string>
#include <vector>

namespace arli {
namespace listing {

inline constexpr const char* kDefaultIgnore =
    "node_modules\n.git\ndist\n.next\nout\n__pycache__\n.venv\nvenv\n"
    "coverage\n.cache\nvendor\ntarget\n";

inline std::vector<std::string> names(const std::string& configured) {
  const auto parsed = shell_policy::lines(configured);
  if (!parsed.empty()) {
    return parsed;
  }
  return shell_policy::lines(kDefaultIgnore);
}

inline bool is_name(const std::string& token, const std::vector<std::string>& skip) {
  for (const auto& name : skip) {
    if (token == name) {
      return true;
    }
  }
  return false;
}

inline bool line_hidden(const std::string& line, const std::vector<std::string>& skip) {
  std::string tok;
  auto hit = [&]() {
    if (tok.empty()) {
      return false;
    }
    const bool yes = is_name(tok, skip);
    tok.clear();
    return yes;
  };
  for (char c : line) {
    if (c == '/' || c == ' ' || c == '\t') {
      if (hit()) {
        return true;
      }
    } else {
      tok.push_back(c);
    }
  }
  return hit();
}

inline bool command_mentions(const std::string& command, const std::vector<std::string>& skip) {
  for (const auto& tok : shell_policy::tokens(command)) {
    if (line_hidden(tok, skip)) {
      return true;
    }
  }
  return false;
}

inline bool is_listing_command(const std::string& command) {
  const auto segs = shell_policy::segments(command);
  if (segs.empty()) {
    return false;
  }
  const auto cmd = shell_policy::tokens(segs.front());
  if (cmd.empty()) {
    return false;
  }
  const std::string& bin = cmd[0];
  return bin == "ls" || bin == "find" || bin == "tree" || bin == "fd" || bin == "dir";
}

inline std::string filter_output(const std::string& output, const std::string& configured) {
  const auto skip = names(configured);
  std::string out;
  std::string line;
  auto flush = [&]() {
    if (!line_hidden(line, skip)) {
      out += line;
      out.push_back('\n');
    }
    line.clear();
  };
  for (char c : output) {
    if (c == '\n') {
      flush();
    } else if (c != '\r') {
      line.push_back(c);
    }
  }
  if (!line.empty()) {
    flush();
  }
  return out;
}

inline std::string rg_globs(const std::string& configured) {
  std::string out;
  for (const auto& name : names(configured)) {
    out += " --glob '!**/" ;
    out += name;
    out += "/**' --glob '!";
    out += name;
    out += "'";
  }
  return out;
}

}  // namespace listing
}  // namespace arli
