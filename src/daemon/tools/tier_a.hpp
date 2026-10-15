#pragma once

#include "tools.hpp"

#include <filesystem>
#include <string>

namespace arli {

ToolResult tool_glob_files(const std::filesystem::path& workdir, const std::string& pattern,
                           const std::string& path, const std::string& list_ignore);

ToolResult tool_file_stat(const std::filesystem::path& workdir, const std::string& path);

ToolResult tool_search_files(const std::filesystem::path& workdir, const std::string& name_substring,
                             const std::string& path, const std::string& list_ignore);

ToolResult tool_summarize_dir(const std::filesystem::path& workdir, const std::string& path,
                              const std::string& list_ignore);

ToolResult tool_quote_file(const std::filesystem::path& workdir, const std::string& path, int start_line,
                           int end_line);

ToolResult tool_make_dir(const std::filesystem::path& workdir, const std::string& path);

ToolResult tool_copy_file(const std::filesystem::path& workdir, const std::string& src, const std::string& dest);

ToolResult tool_move_file(const std::filesystem::path& workdir, const std::string& src, const std::string& dest);

ToolResult tool_delete_file(const std::filesystem::path& workdir, const std::string& path);

ToolResult tool_delete_dir(const std::filesystem::path& workdir, const std::string& path);

ToolResult tool_restore_file(const std::filesystem::path& workdir, const std::string& path);

}  // namespace arli
