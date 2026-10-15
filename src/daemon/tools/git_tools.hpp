#pragma once

#include "tools.hpp"

#include <filesystem>
#include <string>

namespace arli {

ToolResult tool_git_status(const std::filesystem::path& workdir);

ToolResult tool_git_diff(const std::filesystem::path& workdir, const std::string& path);

ToolResult tool_git_log(const std::filesystem::path& workdir, int max_count);

}  // namespace arli
