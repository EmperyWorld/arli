#pragma once
#include "shell_run.hpp"
#include "host_runtime.hpp"
#include "../paths.hpp"
#include "../private_state.hpp"
#include <stdexcept>
#include <vector>
#include <unistd.h>

namespace arli {
struct ShellLaunch {
  std::string cwd;
  std::vector<std::string> argv;
  std::vector<std::string> env{"PATH=/usr/bin:/bin", "HOME=/tmp", "LANG=C.UTF-8", "TMPDIR=/tmp"};
};
inline ShellLaunch prepare_shell(const std::filesystem::path& workdir, const std::string& command,
                                 const ShellRunOptions& opts, bool detached = false) {
  ShellLaunch launch;
  launch.cwd = std::filesystem::canonical(workdir).string();
  guard_private_state(launch.cwd);
  std::string bwrap;
  for (const char* p : {"/usr/bin/bwrap", "/bin/bwrap"}) if (access(p, X_OK) == 0) { bwrap = p; break; }
  if (bwrap.empty()) {
    if (!opts.allow_unsandboxed || opts.read_only)
      throw std::runtime_error("bubblewrap required for this execution policy");
    launch.argv = {"/bin/sh", "-c", command};
    return launch;
  }
  launch.argv = {bwrap, "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
      "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
      "--symlink", "usr/lib64", "/lib64", "--tmpfs", "/tmp",
      opts.read_only ? "--ro-bind" : "--bind", launch.cwd, launch.cwd,
      "--unshare-pid", "--unshare-ipc", "--unshare-uts", "--proc", "/proc", "--dev", "/dev",
      "--new-session", "--chdir", launch.cwd};
  if (!detached) launch.argv.push_back("--die-with-parent");
  // Mount only the selected runtime binary, never nvm's/user's home directory.
  // A fixed PATH prevents a project-local node from shadowing the host interpreter.
  if (const auto node = host_interpreter(workdir, "node"); !node.empty()) {
    launch.argv.insert(launch.argv.end(), {"--dir", "/run/arli/bin", "--ro-bind", node, "/run/arli/bin/node"});
    launch.env[0] = "PATH=/run/arli/bin:/usr/bin:/bin";
  }
  // Never expose the state directory if a user selects an ancestor as the workspace.
  const auto state = std::filesystem::weakly_canonical(state_root());
  const auto rel = state.lexically_relative(launch.cwd);
  if (!rel.empty() && *rel.begin() != "..")
    launch.argv.insert(launch.argv.end(), {"--tmpfs", state.string()});
  if (!opts.shell_net || opts.read_only) launch.argv.push_back("--unshare-net");
  launch.argv.insert(launch.argv.end(), {"--", "/bin/sh", "-c", command});
  return launch;
}
inline std::vector<char*> process_pointers(std::vector<std::string>& values) {
  std::vector<char*> pointers;
  for (auto& value : values) pointers.push_back(value.data());
  pointers.push_back(nullptr);
  return pointers;
}
}
