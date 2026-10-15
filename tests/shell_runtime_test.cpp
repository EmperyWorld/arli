#include "tools.hpp"
#include "tools/host_runtime.hpp"
#include "tools/process_policy.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; } }
int main() {
  char tmp[] = "/tmp/arli-shell-runtime-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), wd = root / "workspace", fake_home = root / "private-home";
  fs::create_directories(wd); fs::create_directories(fake_home);
  setenv("ARLI_STATE_DIR", (root / "state").c_str(), 1);
  const auto node = arli::host_interpreter(wd, "node");
  if (node.empty()) { std::cerr << "SKIP optional host Node is not installed\n"; fs::remove_all(root); return 0; }
  std::ofstream(wd / "node") << "#!/bin/sh\necho PROJECT_NODE_MUST_NOT_RUN\nexit 91\n";
  chmod((wd / "node").c_str(), 0755);
  setenv("PATH", wd.c_str(), 1);
  check(arli::host_interpreter(wd, "node") == node, "workspace PATH cannot replace trusted host runtime");
  const auto bin = fake_home / ".nvm/versions/node/v24.21.0/bin";
  fs::create_directories(bin); fs::create_symlink(node, bin / "node");
  std::ofstream(fake_home / "private-marker") << "PRIVATE_TEST_VALUE";
  std::ofstream(fake_home / "preload.cjs") << "throw Error('INHERITED_PRELOAD_MUST_NOT_RUN');";
  setenv("HOME", fake_home.c_str(), 1);
  setenv("NODE_OPTIONS", ("--require=" + (fake_home / "preload.cjs").string()).c_str(), 1);
  setenv("ARLI_INTERNAL_TOKEN", "SYNTHETIC_PRIVATE_TOKEN", 1);
  arli::ShellRunOptions opts; opts.read_only = true; opts.timeout_sec = 10;
  const auto launch = arli::prepare_shell(wd, "node ./runtime.cjs", opts);
  bool runtime_bound = false, home_bound = false;
  for (size_t i = 0; i + 2 < launch.argv.size(); ++i) if (launch.argv[i] == "--ro-bind" || launch.argv[i] == "--bind") {
    runtime_bound |= launch.argv[i] == "--ro-bind" && launch.argv[i + 1] == node && launch.argv[i + 2] == "/run/arli/bin/node";
    home_bound |= launch.argv[i + 1] == fake_home.string();
  }
  check(runtime_bound && !home_bound, "only host runtime binary is mounted read-only, not its home");
  std::ofstream(wd / "runtime.cjs") <<
      "const assert = require('node:assert/strict'); const fs = require('node:fs');\n"
      "assert.equal(2 + 2, 4);\n"
      "assert.equal(process.execPath, '/run/arli/bin/node');\n"
      "assert.equal(process.env.HOME, '/tmp');\n"
      "assert.equal(process.env.NODE_OPTIONS, undefined);\n"
      "assert.equal(process.env.ARLI_INTERNAL_TOKEN, undefined);\n"
      "assert.equal(fs.existsSync(" << json((fake_home / "private-marker").string()).dump() << "), false);\n"
      "const mount = fs.readFileSync('/proc/self/mountinfo', 'utf8').split('\\n').find(x => x.split(' ')[4] === process.execPath);\n"
      "assert.ok(mount && mount.split(' ')[5].split(',').includes('ro'));\n"
      "assert.throws(() => fs.writeFileSync('./must-not-write', 'x'), {code: 'EROFS'});\n"
      "console.log('SANDBOX_NODE_OK');\n";
  const auto result = arli::tool_run_shell(wd, "node ./runtime.cjs", opts);
  if (!result.ok) std::cerr << result.output << '\n';
  check(result.ok && result.output.find("SANDBOX_NODE_OK") != std::string::npos,
        "real host Node runs behavior checks with sanitized environment and hidden home");
  check(!fs::exists(wd / "must-not-write"), "read-only workspace remains immutable");
  // Even the NVM installation path must not resolve into the project itself.
  fs::remove(bin / "node"); fs::create_symlink(wd / "node", bin / "node");
  if (!fs::exists("/usr/bin/node") && !fs::exists("/bin/node") && !fs::exists("/usr/local/bin/node"))
    check(arli::host_interpreter(wd, "node").empty(), "NVM symlink to project executable is rejected");
  fs::remove_all(root);
  return failures ? 1 : 0;
}
