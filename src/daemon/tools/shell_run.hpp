#pragma once

#include <filesystem>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

namespace arli {

struct ToolResult;

struct ShellRunOptions {
  int timeout_sec = 120;
  bool shell_net = false;
  bool allow_unsandboxed = false;
  bool read_only = false;
  std::atomic<bool>* cancel = nullptr;
  std::int64_t dialog_id = 0, task_id = 0, turn_id = 0;
};

namespace tools_internal {
ToolResult run_read_only(const std::filesystem::path& workdir, const std::vector<std::string>& argv, int timeout_sec = 15);
}

ToolResult tool_run_shell(const std::filesystem::path& workdir, const std::string& command,
                          const ShellRunOptions& opts = {});

}  // namespace arli
