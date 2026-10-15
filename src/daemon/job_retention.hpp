#pragma once
#include "db.hpp"
#include <string>
namespace arli {
inline constexpr int kRetainedJobs = 128;
inline constexpr int kJobRetentionDays = 7;
// Removes only the four supervisor-owned names in a private state/jobs/job-XXXXXX.
// Unknown contents, noncanonical paths and unsafe directories fail closed.
bool remove_job_files(const std::string& log_path);
void retain_jobs(Database& db,std::int64_t& cursor);
}
