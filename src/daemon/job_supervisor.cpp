// Dedicated process: owns one command group and bounded logs, survives worker restart.
#include "job_identity.hpp"
#include "job_lease.hpp"
#include "job_output.hpp"
#include "job_channel.hpp"
#include "tools/rlimit_util.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using json = nlohmann::json;
namespace {
volatile sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
struct OwnedChild {
  pid_t pid;
  ~OwnedChild(){if(pid>0){kill(-pid,SIGKILL);kill(pid,SIGKILL);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}}}
};

}
int main() {
  try {
    arli::JobChannel channel(3);
    uint32_t size = 0;
    if (!channel.transfer(&size, sizeof(size), false) || size > 1024 * 1024) return 2;
    std::string encoded(size, '\0'); if (!channel.transfer(encoded.data(), size, false)) return 2;
    const auto config = json::parse(encoded);
    const std::string log = config.at("log"), cwd = config.at("cwd");
    arli::redact::enable_persistence(config.at("fingerprints").get<std::string>());
    const auto identity = arli::process_identity(getpid());
    arli::JobDirectoryLease lease(std::filesystem::path(log).parent_path());
    if(!lease)return 2;
    arli::JobOutput output_log(log);
    if (identity.empty()) return 2;
    char ready = 'R'; if (!channel.transfer(&ready, 1, true) || !channel.transfer(&ready, 1, false) || ready != 'G') return 2;
    close(3);
    signal(SIGTERM, stop); signal(SIGINT, stop); signal(SIGPIPE, SIG_IGN);
    auto args = config.at("argv").get<std::vector<std::string>>();
    auto env = config.at("env").get<std::vector<std::string>>();
    std::vector<char*> argv, envp;
    for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    for (auto& item : env) envp.push_back(item.data()); envp.push_back(nullptr);
    int output[2]; if (pipe2(output, O_CLOEXEC) != 0) return 2;
    const pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
      signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGPIPE, SIG_DFL);
      if (setsid() < 0 || chdir(cwd.c_str()) != 0) _exit(127);
      dup2(output[1], 1); dup2(output[1], 2);
      close_range(3, ~0U, 0); arli::apply_shell_rlimits();
      execve(argv[0], argv.data(), envp.data()); _exit(127);
    }
    OwnedChild owned{child};
    close(output[1]); fcntl(output[0], F_SETFL, O_NONBLOCK);
    bool terminating = false; int status = 0;
    auto deadline = std::chrono::steady_clock::time_point::max();
    auto drain = [&] {
      char bytes[8192];
      for (int batch = 0; batch < 32; ++batch) {
        const auto n = read(output[0], bytes, sizeof(bytes)); if (n <= 0) break;
        output_log.push(std::string_view(bytes,static_cast<size_t>(n)));
      }
    };
    for (;;) {
      if (stopping && !terminating) {
        // Child may not have called setsid yet; retry below until it exits.
        terminating = true; deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
      }
      if (terminating) kill(-child, std::chrono::steady_clock::now() >= deadline ? SIGKILL : SIGTERM);
      drain();
      siginfo_t info{};
      if (waitid(P_PID, child, &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid == child) {
        // Keep the leader unreaped until group cleanup: its PID cannot be reused.
        kill(-child, SIGKILL);
        pid_t reaped;do {reaped=waitpid(child,&status,0);}while(reaped<0&&errno==EINTR);
        if(reaped!=child)throw std::runtime_error("job reap failed");owned.pid=0;
        drain(); // Read bytes written between the preceding drain and child exit.
        break;
      }
      pollfd wait{output[0], POLLIN, 0}; poll(&wait, 1, 50);
    }
    close(output[0]); output_log.finish(stopping || WIFSIGNALED(status));
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    const json result = {{"identity", identity}, {"exit_code", code}, {"stopped", bool(stopping)}};
    const std::string temp = log + ".result.tmp", target = log + ".result";
    { std::ofstream out(temp); out << result.dump(); out.flush(); if (!out) return 2; }
    const int resultfd = open(temp.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (resultfd < 0) return 2;
    const int synced = fsync(resultfd); close(resultfd); if (synced != 0) return 2;
    std::filesystem::rename(temp, target);
    const int dirfd = open(std::filesystem::path(log).parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd < 0) return 2;
    const int dirsync = fsync(dirfd); close(dirfd); if (dirsync != 0) return 2;
    return 0;
  } catch (...) { return 2; }
}
