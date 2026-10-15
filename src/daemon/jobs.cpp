#include "jobs.hpp"
#include "job_identity.hpp"
#include <nlohmann/json.hpp>
#include <sys/syscall.h>
#include <poll.h>
#include "tools/process_policy.hpp"
#include "tools/rlimit_util.hpp"

#include "db.hpp"
#include "listing.hpp"
#include "paths.hpp"
#include "shell_policy.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace arli {
std::string job_log_tail(const std::string& path, std::size_t max_bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  in.seekg(0, std::ios::end);
  const auto end = static_cast<std::streamoff>(in.tellg());
  if (end <= 0) {
    return {};
  }
  const auto keep = static_cast<std::streamoff>(max_bytes);
  in.seekg(end > keep ? end - keep : std::streamoff(0));
  std::stringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

ToolResult execute_shell(Database& db, const std::filesystem::path& workdir, const std::string& command,
                         const std::string& list_ignore, bool force_background, const ShellRunOptions& shell) {
  if (command.empty()) {
    return {false, "empty command"};
  }
  if (force_background || shell_policy::is_background(command)) {
    try { return start_background(db, workdir, command, shell); }
    catch (const std::exception& e) { return {false, e.what()}; }
  }
  ToolResult result = tool_run_shell(workdir, command, shell);
  if (listing::is_listing_command(command) && !listing::command_mentions(command, listing::names(list_ignore))) {
    result.output = listing::filter_output(result.output, list_ignore);
  }
  return result;
}

JobRow refresh_job(Database& db, JobRow job) {
  if (job.status != "running") return job;
  auto completed = [&] {
    try {
      std::ifstream input(job.log_path + ".result"); nlohmann::json result;
      if (input && (input >> result) && !job.process_identity.empty() && result.value("identity", "") == job.process_identity) {
        job.status = result.value("stopped", false) ? "stopped" : "exited";
        job.exit_code = result.at("exit_code"); db.finish_job(job.id, job.status, job.exit_code);
        if(auto saved=db.get_job(job.id))job=*saved;return true;
      }
    } catch (...) {}
    return false;
  };
  if (completed()) return job;
  if (job.process_identity.empty() || process_identity(job.pid) != job.process_identity) {
    // Publication precedes supervisor exit; retry after observing its disappearance.
    if (completed()) return job;
    job.status = "lost"; db.set_job_status(job.id, job.status);
    if(auto saved=db.get_job(job.id))job=*saved;
  }
  return job;
}
bool stop_job_by_id(Database& db, std::int64_t id) {
  const auto stored = db.get_job(id);
  if (!stored) return false;
  auto job = refresh_job(db, *stored);
  if (job.status != "running" || job.process_identity.empty()) return false;
  const int fd = static_cast<int>(syscall(SYS_pidfd_open, job.pid, 0));
  if (fd < 0) return false;
  // pidfd is opened first; a subsequent identity check rejects PID reuse before open.
  const bool same = process_identity(job.pid) == job.process_identity;
  const bool sent = same && syscall(SYS_pidfd_send_signal, fd, SIGTERM, nullptr, 0) == 0;
  if (sent) { pollfd p{fd, POLLIN, 0}; poll(&p, 1, 2500); }
  close(fd);
  job = refresh_job(db, job);
  return sent && job.status == "stopped";
}

}  // namespace arli
