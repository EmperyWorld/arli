#include "tools/syntax_check.hpp"
#include "read_edit_guard.hpp"
#include "verification.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using nlohmann::json;
int main() {
  char temp[] = "/tmp/arli-verification-replay-XXXXXX";
  if (!mkdtemp(temp)) return 2;
  const fs::path workdir(temp);
  setenv("ARLI_STATE_DIR", (workdir / ".state").c_str(), 1);
  const std::string path = "config.json", original = "{\"enabled\":false}\n";
  std::ofstream(workdir / path) << original;
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
  };
  arli::VerificationState state;
  json args = {{"path", path}, {"start_line", 1}, {"end_line", 1}, {"text", "{\"enabled\":"}};
  auto result = arli::tool_edit_lines(workdir, path, 1, 1, args["text"], &original);
  check(!result.ok && result.output.find("SYNTAX_REJECTED:") != std::string::npos,
        "real malformed edit is rejected before writing");
  state.file_result("edit_file", path, result.ok, result.output, args);
  result = arli::tool_check_file(workdir, path);
  check(result.ok, "old source genuinely remains syntactically valid");
  state.file_result("check_file", path, result.ok, result.output);
  check(!state.pending().empty(), "incident replay: valid old source cannot close real rejected edit");
  std::ifstream unchanged(workdir / path);
  check(std::string(std::istreambuf_iterator<char>(unchanged), {}) == original,
        "incident replay leaves original source unchanged");
  arli::VerificationState continued;
  check(continued.restore(state.serialized()), "carry unresolved actual operation into continuation");
  args["text"] = "{\"enabled\":true}";
  result = arli::tool_edit_lines(workdir, path, 1, 1, args["text"], &original);
  if (!result.ok) std::cerr << result.output << '\n';
  check(result.ok, "real correcting edit is accepted");
  continued.file_result("edit_file", path, result.ok, result.output, args);
  check(continued.pending().empty(), "actual correction resolves corresponding durable pending operation");
  std::ifstream changed(workdir / path);
  json parsed; changed >> parsed;
  check(parsed.value("enabled", false), "correcting edit actually wrote the intended value");
  {
    std::ofstream(workdir / "note.txt") << "head\n";
    arli::ReadEditGuard guard;
    arli::VerificationState append;
    const json append_args = {{"path", "note.txt"}, {"content", "tail\n"}};
    auto retry = guard.edit(workdir, "append_file", append_args.dump());
    check(!retry.ok && retry.output.find("READ_REQUIRED:") != std::string::npos, "real unread append is rejected");
    append.file_result("append_file", "note.txt", retry.ok, retry.output, append_args);
    guard.record(workdir, "note.txt", arli::tool_read_file(workdir, "note.txt").output);
    retry = guard.edit(workdir, "append_file", append_args.dump());
    check(retry.ok, "fresh source read permits legitimate append retry");
    append.file_result("append_file", "note.txt", retry.ok, retry.output, append_args);
    check(append.pending().empty(), "actual successful append retry clears only its rejected operation");
  }
  fs::remove_all(workdir);
  return failures ? 1 : 0;
}
