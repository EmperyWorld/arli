#include "syntax_check.hpp"
#include "host_runtime.hpp"
#include "context.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace arli {
namespace {
SyntaxCheck parse(const std::vector<std::string>& command, const std::string& content) {
  // An unlinked, private file avoids pipe deadlocks for large source input.
  char name[] = "/tmp/arli-syntax-XXXXXX";
  const int input = mkstemp(name);
  if (input < 0) return {true, false, "cannot create parser input"};
  unlink(name); fcntl(input, F_SETFD, FD_CLOEXEC);
  size_t offset = 0;
  while (offset < content.size()) {
    const auto n = write(input, content.data() + offset, content.size() - offset);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { close(input); return {true, false, "cannot write parser input"}; }
    offset += static_cast<size_t>(n);
  }
  lseek(input, 0, SEEK_SET);
  int output[2];
  if (pipe2(output, O_CLOEXEC) != 0) { close(input); return {true, false, "parser pipe failed"}; }
  std::vector<char*> argv;
  for (const auto& arg : command) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  // In particular, do not inherit NODE_OPTIONS, PYTHONPATH, LD_PRELOAD or secrets.
  char env_path[] = "PATH=/usr/bin:/bin", env_lang[] = "LANG=C.UTF-8";
  char* env[] = {env_path, env_lang, nullptr};
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(input, STDIN_FILENO); dup2(output[1], STDOUT_FILENO); dup2(output[1], STDERR_FILENO);
    close(input); close(output[0]); close(output[1]);
    struct rlimit cpu{5, 5}, memory{4ULL << 30, 4ULL << 30}, files{0, 0};
    setrlimit(RLIMIT_CPU, &cpu); setrlimit(RLIMIT_AS, &memory);
    setrlimit(RLIMIT_FSIZE, &files); setrlimit(RLIMIT_CORE, &files);
    if (chdir("/") != 0) _exit(127);
    execve(argv[0], argv.data(), env); _exit(127);
  }
  close(input); close(output[1]);
  if (pid < 0) { close(output[0]); return {true, false, "parser fork failed"}; }
  fcntl(output[0], F_SETFL, O_NONBLOCK);
  std::string detail; std::array<char, 2048> buffer{};
  auto drain = [&] {
    for (;;) {
      const auto n = read(output[0], buffer.data(), buffer.size());
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      if (detail.size() < 12000) detail.append(buffer.data(), std::min<size_t>(n, 12000 - detail.size()));
    }
  };
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  int status = 0; bool timeout = false;
  for (;;) {
    drain();
    const auto waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) break;
    if (waited < 0 && errno != EINTR) { status = -1; break; }
    if (std::chrono::steady_clock::now() >= deadline) {
      timeout = true; kill(pid, SIGKILL);
      while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      break;
    }
    usleep(10000);
  }
  drain(); close(output[0]);
  if (timeout) return {true, false, "parser timeout"};
  if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) return {true, true, "syntax OK"};
  return {true, false, detail.empty() ? "parser could not complete" : context::clip_text(detail, 6000)};
}

SyntaxCheck js_diagnostic(SyntaxCheck result, const std::string& mode, const std::string& path,
                          const std::string& content) {
  if (result.ok) { result.detail = "JavaScript (" + mode + ") syntax OK"; return result; }
  std::string detail = result.detail;
  int line_number = 0;
  const auto location = detail.find("[stdin]:");
  if (location != std::string::npos) {
    size_t end = location + 8;
    while (end < detail.size() && std::isdigit(static_cast<unsigned char>(detail[end]))) ++end;
    try { line_number = std::stoi(detail.substr(location + 8, end - location - 8)); } catch (...) {}
    detail.erase(0, location); // Drop unrelated loader warnings before the actual diagnostic.
  }
  if (const auto stack = detail.find("\n    at "); stack != std::string::npos) detail.erase(stack);
  if (const auto version = detail.find("\nNode.js "); version != std::string::npos) detail.erase(version);
  for (size_t pos = 0; (pos = detail.find("[stdin]", pos)) != std::string::npos; pos += path.size())
    detail.replace(pos, 7, path);
  result.detail = "JavaScript (" + mode + "), candidate " + path + "\n" + context::clip_text(detail, 2800);
  if (line_number > 0) {
    result.detail += "\nCandidate context (new file, not yet written):\n";
    std::istringstream input(content); std::string line;
    for (int n = 1; n <= line_number + 2 && std::getline(input, line); ++n)
      if (n >= line_number - 2)
        result.detail += std::to_string(n) + "|" + context::clip_text(line, 400) + "\n";
  }
  return result;
}
}

SyntaxCheck check_source_syntax(const std::filesystem::path& workdir, const std::string& path,
                                const std::string& content) {
  const auto ext = std::filesystem::path(path).extension().string();
  if (ext == ".json") {
    try { const auto parsed = nlohmann::json::parse(content); (void)parsed; return {true, true, "JSON syntax OK"}; }
    catch (const std::exception& e) { return {true, false, e.what()}; }
  }
  if (ext == ".js" || ext == ".mjs" || ext == ".cjs") {
    const auto node = host_interpreter(workdir, "node");
    if (node.empty()) return {false, true, "SYNTAX_UNCHECKED: установи nodejs для проверки JavaScript"};
    auto result = parse({node, "--check", ext == ".mjs" ? "--input-type=module" : "--input-type=commonjs"}, content);
    // Plain .js can be either a browser script or an ES module. This is syntax validation, not runtime validation.
    if (!result.ok && ext == ".js") {
      auto module = parse({node, "--check", "--input-type=module"}, content);
      // A rejected import/export in CJS is a mode error, not the candidate's real error.
      // In all other ambiguous failures report both parsers, rather than guessing a mode.
      if (module.ok || result.detail.find("Unexpected token 'export'") != std::string::npos ||
          result.detail.find("Cannot use import statement outside a module") != std::string::npos)
        return js_diagnostic(std::move(module), "ES module", path, content);
      auto esm = js_diagnostic(std::move(module), "ES module", path, content);
      auto cjs = js_diagnostic(std::move(result), "CommonJS", path, content);
      return {true, false, "JavaScript mode is ambiguous; both parsers rejected the candidate.\n" +
          esm.detail + "\n" + cjs.detail};
    }
    return js_diagnostic(std::move(result), ext == ".mjs" ? "ES module" : "CommonJS", path, content);
  }
  if (ext == ".py") {
    const auto python = host_interpreter(workdir, "python3");
    if (python.empty()) return {false, true, "SYNTAX_UNCHECKED: установи python3 для проверки Python"};
    return parse({python, "-I", "-S", "-c", "import ast,sys; ast.parse(sys.stdin.buffer.read(), filename='<candidate>')"}, content);
  }
  return {false, true, "SYNTAX_UNCHECKED: для этого типа файла встроенной проверки нет"};
}

ToolResult tool_check_file(const std::filesystem::path& workdir, const std::string& path) {
  try {
    const auto file = resolve_under(workdir, path);
    if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > 8 * 1024 * 1024)
      return {false, "check_file: нужен обычный файл до 8 MiB"};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {false, "check_file: cannot read " + path};
    const std::string source{std::istreambuf_iterator<char>(in), {}};
    const auto result = check_source_syntax(workdir, path, source);
    return {result.checked && result.ok, path + ": " + result.detail +
        (result.checked && result.ok ? ". Проверен только синтаксис; поведение требует тестов." : "")};
  } catch (const std::exception& e) { return {false, e.what()}; }
}
}
