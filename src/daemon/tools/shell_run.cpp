#include "shell_run.hpp"
#include "process_policy.hpp"
#include <cerrno>
#include <signal.h>

#include "tools.hpp"

#include "rlimit_util.hpp"
#include "shell_lex.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <vector>

#include <sys/wait.h>

namespace arli {
namespace {

ToolResult run_sh_child(ShellLaunch launch, const ShellRunOptions& opts) {
  auto cargv = process_pointers(launch.argv);
  auto cenv = process_pointers(launch.env);
  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return {false, "pipe failed"};
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return {false, "fork failed"};
  }
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    const int input = open("/dev/null", O_RDONLY);
    if (input >= 0) { dup2(input, STDIN_FILENO); if (input > 2) close(input); }
    close_range(3, ~0U, 0);
    setpgid(0, 0);
    apply_shell_rlimits();
    if (chdir(launch.cwd.c_str()) != 0) {
      _exit(127);
    }
    execve(cargv[0], cargv.data(), cenv.data());
    _exit(127);
  }
  close(pipefd[1]);
  setpgid(pid, pid);
  std::string out;
  std::array<char, 16384> buf{};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(opts.timeout_sec > 0 ? opts.timeout_sec : 120);
  fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
  int status = 0;
  bool done = false;
  while (!done) {
    const ssize_t n = read(pipefd[0], buf.data(), buf.size());
    if (n > 0) {
      out.append(buf.data(), static_cast<size_t>(n));
      if (out.size() > 200000) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        close(pipefd[0]);
        out.resize(200000);
        return {false, "output limit exceeded\n" + out};
      }
    }
    const pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
      done = true;
    } else if (w < 0 && errno != EINTR) {
      kill(-pid, SIGKILL); close(pipefd[0]); return {false, "waitpid failed"};
    } else if ((opts.cancel && opts.cancel->load()) || std::chrono::steady_clock::now() > deadline) {
      kill(-pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      close(pipefd[0]);
      return {false, (opts.cancel && opts.cancel->load() ? "cancelled\n" : "timeout\n") + out};
    } else {
      usleep(20000);
    }
  }
  while (true) {
    const ssize_t n = read(pipefd[0], buf.data(), buf.size());
    if (n <= 0) {
      break;
    }
    out.append(buf.data(), static_cast<size_t>(n));
    if (out.size() > 200000) {
      kill(-pid, SIGKILL); close(pipefd[0]); out.resize(200000);
      return {false, "output limit exceeded\n" + out};
    }
  }
  close(pipefd[0]);
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (code == 0) {
    return {true, out.empty() ? "(ok, empty stdout)" : out};
  }
  return {false, "exit " + std::to_string(code) + "\n" + out};
}

}  // namespace

ToolResult tools_internal::run_read_only(const std::filesystem::path& workdir, const std::vector<std::string>& argv, int timeout_sec) {
  if (argv.empty() || argv.front().empty() || argv.front().front() != '/') return {false, "fixed absolute executable required"};
  try {
    ShellRunOptions options; options.read_only = true; options.timeout_sec = timeout_sec;
    auto launch = prepare_shell(workdir, "", options);
    launch.argv.resize(launch.argv.size() - 3); // Remove /bin/sh -c and execute fixed argv directly.
    launch.argv.insert(launch.argv.end(), argv.begin(), argv.end());
    return run_sh_child(launch, options);
  } catch (const std::exception& e) { return {false, e.what()}; }
}

ToolResult tool_run_shell(const std::filesystem::path& workdir, const std::string& command,
                          const ShellRunOptions& opts) {
  if (command.empty()) {
    return {false, "empty command"};
  }
  if (shell_mutates_files(command)) {
    return {false,
            "shell не для записи файлов (>, >>, tee, sed -i, cp/mv…). "
            "Используй edit_file / create_file / append_file."};
  }
  try {
    std::filesystem::create_directories(workdir);
    return run_sh_child(prepare_shell(workdir, command, opts), opts);
  } catch (const std::exception& e) {
    return {false, e.what()};
  }
}

}  // namespace arli
