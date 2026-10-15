#pragma once

#include "tools.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace arli {

class Database;
struct JobRow;
JobRow refresh_job(Database& db, JobRow job);
ToolResult start_background(Database&, const std::filesystem::path&, const std::string&, const ShellRunOptions&);

// Run a shell command. A background command (`&`, nohup, or forced) is started
// in its own session, recorded in `jobs`, and returned at once. Listing commands
// (`ls`, `find`, `tree`, `fd`) drop ignored directory names unless the command
// names that directory itself.
ToolResult execute_shell(Database& db, const std::filesystem::path& workdir, const std::string& command,
                         const std::string& list_ignore, bool force_background,
                         const ShellRunOptions& shell = {});

// SIGTERM the process group, then SIGKILL if it is still alive. Marks the row stopped.
bool stop_job_by_id(Database& db, std::int64_t id);

std::string job_log_tail(const std::string& path, std::size_t max_bytes = 2000);

}  // namespace arli
