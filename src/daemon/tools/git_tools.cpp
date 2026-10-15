#include "git_tools.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <vector>

#include <unistd.h>

#include <sys/wait.h>

namespace arli {
namespace {

ToolResult run_git(const std::filesystem::path& cwd, std::vector<std::string> args) {
  const std::vector<std::string> prefix = {"/usr/bin/git", "--no-pager", "--no-optional-locks",
      "-c", "core.fsmonitor=false", "-c", "core.hooksPath=/dev/null"};
  args.insert(args.begin(), prefix.begin(), prefix.end());
  return tools_internal::run_read_only(cwd, args, 15);
}

}  // namespace

ToolResult tool_git_status(const std::filesystem::path& workdir) {
  try {
    const auto root = resolve_under(workdir, ".");
    return run_git(root, {"status", "--short"});
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_git_diff(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto root = resolve_under(workdir, ".");
    if (path.empty()) {
      return run_git(root, {"diff", "--no-ext-diff", "--no-textconv", "--stat"});
    }
    const auto rel = resolve_under(workdir, path);
    std::error_code ec;
    const auto r = std::filesystem::relative(rel, root, ec);
    return run_git(root, {"diff", "--no-ext-diff", "--no-textconv", "--", ec ? path : r.generic_string()});
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

ToolResult tool_git_log(const std::filesystem::path& workdir, int max_count) {
  try {
    const auto root = resolve_under(workdir, ".");
    if (max_count <= 0) {
      max_count = 20;
    }
    if (max_count > 100) {
      max_count = 100;
    }
    return run_git(root, {"log", "-n", std::to_string(max_count), "--oneline"});
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

}  // namespace arli
