#include "tools/backup_store.hpp"
#include "tools/syntax_check.hpp"
#include "read_edit_guard.hpp"
#include "verification.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;
using json = nlohmann::json;
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; } }
std::string read(const fs::path& path) { std::ifstream in(path); return {std::istreambuf_iterator<char>(in), {}}; }
int main() {
  char tmp[] = "/tmp/arli-syntax-test-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path wd(tmp);
  check(!arli::tool_create_file(wd, "bad.json", "{broken").ok && !fs::exists(wd / "bad.json"), "invalid JSON never created");
  check(arli::tool_create_file(wd, "config.json", "{\"n\":1}\n").ok, "valid JSON writes");
  check(!arli::tool_edit_lines(wd, "config.json", 1, 1, "{\"n\":}").ok, "invalid JSON edit rejected");
  check(read(wd / "config.json") == "{\"n\":1}\n" && !fs::exists(arli::tools_internal::backup_path(wd, wd / "config.json")), "rejection leaves file and backup intact");
  check(!arli::tool_append_file(wd, "config.json", "garbage").ok, "append validates complete assembled file");
  check(arli::dispatch_tool(wd, "check_file", "{\"path\":\"config.json\"}", {}, true).ok, "check_file is read-only");
  check(!arli::tool_check_file(wd, "../outside.json").ok, "check_file respects workdir boundary");
  check(!arli::tool_check_file(wd, ".").ok, "check_file rejects directories");
  std::ofstream(wd / "note.txt") << "anything";
  check(!arli::tool_check_file(wd, "note.txt").ok, "unsupported type never claimed validated");
  const auto node = arli::check_source_syntax(wd, "test.js", "const value = 1;\n");
  if (node.checked) {
    check(node.ok, "Node parser available");
    const std::string original = "class Manager {\n  run() { return 1; }\n}\n";
    std::ofstream(wd / "script.js") << original;
    // The observed failure: insert an entire class instead of a method inside an existing class.
    const auto wrong = arli::tool_edit_lines(wd, "script.js", 2, 2, "class Manager {\n  run() { return 2; }\n}");
    check(!wrong.ok && wrong.output.find("SYNTAX_REJECTED") != std::string::npos, "misplaced class rejected before commit");
    check(read(wd / "script.js") == original, "wrong boundary does not damage source");
    check(!fs::exists(arli::tools_internal::backup_path(wd, wd / "script.js")), "wrong edit cannot replace last good backup");
    check(!arli::tool_edit_file(wd, "script.js", "return 1;", "return (; ").ok, "legacy search edit uses same guard");
    check(arli::tool_edit_lines(wd, "script.js", 2, 2, "  run() { return 2; }").ok, "correct localized edit commits");
    check(read(arli::tools_internal::backup_path(wd, wd / "script.js")) == original, "successful edit preserves previous source");
    std::ofstream(wd / "hook.cjs") << "require('fs').writeFileSync('" << (wd / "executed").string() << "','bad');";
    const char* old = std::getenv("NODE_OPTIONS"); const std::string saved = old ? old : "";
    setenv("NODE_OPTIONS", ("--require=" + (wd / "hook.cjs").string()).c_str(), 1);
    check(arli::check_source_syntax(wd, "test.cjs", read(wd / "hook.cjs")).ok, "parser ignores preload options");
    if (old) setenv("NODE_OPTIONS", saved.c_str(), 1); else unsetenv("NODE_OPTIONS");
    check(!fs::exists(wd / "executed"), "neither source nor environment preload executes");
    check(arli::check_source_syntax(wd, "module.mjs", "export const x = 1;").ok, "ES module syntax supported");
    check(!arli::check_source_syntax(wd, "module.cjs", "export const x = 1;").ok, "CJS rejects module syntax");
    std::ofstream(wd / "broken.js") << "class A { class A {} }\n";
    check(!arli::tool_check_file(wd, "broken.js").ok, "existing corruption diagnosed");
    check(arli::tool_edit_lines(wd, "broken.js", 1, 1, "class A {}").ok, "complete repair of preexisting corruption allowed");
    const auto fixture = fs::path(__FILE__).parent_path() / "fixtures/dialog18-rejected-edits.json";
    const auto incidents = json::parse(read(fixture));
    check(incidents.size() == 4, "all four historical rejected candidates present");
    for (const auto& incident : incidents) {
      const auto path = incident["path"].get<std::string>();
      const auto before = incident["before"].get<std::string>();
      const auto& args = incident["args"];
      fs::create_directories((wd / path).parent_path());
      std::ofstream(wd / path) << before;
      const auto backup = arli::tools_internal::backup_path(wd, wd / path);
      fs::create_directories(backup.parent_path()); std::ofstream(backup) << "prior backup";
      const auto rejected = arli::tool_edit_lines(wd, path, args["start_line"], args["end_line"], args["text"]);
      check(!rejected.ok && rejected.output.find("SYNTAX_REJECTED") != std::string::npos,
            "historical malformed edit still rejected");
      check(rejected.output.find("JavaScript (ES module)") != std::string::npos &&
            rejected.output.find(incident["error"].get<std::string>()) != std::string::npos &&
            rejected.output.find(path + ":" + std::to_string(incident["line"].get<int>())) != std::string::npos &&
            rejected.output.find("Candidate context") != std::string::npos,
            "historical rejection explains real ESM error at candidate path and line");
      check(rejected.output.find("Unexpected token 'export'") == std::string::npos,
            "historical ESM rejection does not misreport CommonJS export error");
      check(read(wd / path) == before && read(backup) == "prior backup", "historical rejection preserves file and prior backup");
    }
    const auto ambiguous = arli::check_source_syntax(wd, "ambiguous.js", "const value = (;\n");
    check(!ambiguous.ok && ambiguous.detail.find("ES module") != std::string::npos &&
          ambiguous.detail.find("CommonJS") != std::string::npos, "unknown mode labels both parser failures");
    const auto imported = arli::check_source_syntax(wd, "imported.js", "import x from './x.js';\nconst value = (;\n");
    check(!imported.ok && imported.detail.find("ES module") != std::string::npos &&
          imported.detail.find("imported.js:2") != std::string::npos &&
          imported.detail.find("Cannot use import statement") == std::string::npos,
          "import mode mismatch does not mask actual candidate error");
  } else std::cerr << "SKIP Node parser: " << node.detail << '\n';
  const auto py = arli::check_source_syntax(wd, "source.py", "import nonexistent_module\nraise Exception('must not run')\n");
  check(py.checked && py.ok, "Python parser does not import or execute source");
  check(!arli::check_source_syntax(wd, "source.py", "def broken(:\n").ok, "Python syntax error detected");
  {
    std::ofstream(wd / "one.txt") << "first\nlast\n";
    std::ofstream(wd / "two.txt") << "other\n";
    arli::ReadEditGuard gate;
    gate.record(wd, "one.txt", arli::tool_read_file(wd, "one.txt").output);
    gate.record(wd, "two.txt", arli::tool_read_file(wd, "two.txt").output);
    json args = {{"path", "one.txt"}, {"start_line", 1}, {"end_line", 2}, {"text", "new\nlast"},
                 {"first_line", "wrong"}, {"last_line", "last"}};
    check(!gate.edit(wd, "edit_file", args.dump()).ok && read(wd / "one.txt") == "first\nlast\n", "stale/wrong anchor rejected");
    args["first_line"] = "first";
    check(gate.edit(wd, "edit_file", args.dump()).ok, "matching boundaries authorize read range");
    check(!gate.edit(wd, "edit_file", args.dump()).ok, "same file still requires new read");
    check(gate.edit(wd, "edit_file", json{{"path", "two.txt"}, {"start_line", 1}, {"end_line", 1}, {"text", "changed"}}.dump()).ok,
          "writing one file does not invalidate fresh reads of another");
    arli::ReadEditGuard unchecked;
    arli::tool_check_file(wd, "config.json");
    check(!unchecked.edit(wd, "edit_file", json{{"path", "config.json"}, {"start_line", 1}, {"end_line", 1}, {"text", "{}"}}.dump()).ok,
          "syntax check does not grant read permission");
  }
  {
    arli::VerificationState state;
    state.file_result("check_file", "script.js", false, "syntax error");
    check(!state.pending().empty(), "failed syntax check blocks success");
    state.file_result("edit_file", "other.js", true, "SYNTAX_OK:");
    check(!state.pending().empty(), "unrelated edit cannot clear failure");
    state.file_result("edit_file", "script.js", true, "SYNTAX_OK:");
    check(state.pending().empty(), "validated repair clears same-file failure");
    state.file_result("edit_file", "script.js", false, "SYNTAX_REJECTED:");
    check(!state.pending().empty(), "rejected mutation cannot be reported completed");
  }
  {
    arli::VerificationState state;
    state.observe("./node runtime-check.cjs", false, "exit 1");
    check(!state.pending().empty(), "runtime assertion failure tracked");
    state.mutation();
    check(state.due() == "./node runtime-check.cjs", "mutation schedules the known runtime check");
    state.observe("./node runtime-check.cjs", true, "OK");
    check(state.pending().empty(), "successful runtime recheck clears failure");
    state.observe("node --check source.js", false, "operator declined");
    check(state.blocked() && !state.pending().empty(), "declined first validation is not reported passed");
  }
  fs::remove_all(wd);
  return failures ? 1 : 0;
}
