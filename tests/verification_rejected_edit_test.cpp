#include "verification.hpp"
#include <iostream>

using nlohmann::json;
namespace {
int failures = 0;
void check(bool ok, const char* message) {
  if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
json range(int first, int last) { return {{"start_line", first}, {"end_line", last}, {"text", "unused"}}; }
}
int main() {
  {
    arli::VerificationState state;
    state.file_result("edit_file", "script.js", false, "SYNTAX_REJECTED:", range(20, 29));
    state.file_result("check_file", "script.js", true, "SYNTAX_OK:");
    check(!state.pending().empty(), "checking unchanged valid source cannot complete rejected candidate");
    state.file_result("edit_file", "script.js", true, "SYNTAX_OK:", range(20, 25));
    check(!state.pending().empty(), "partial overlapping correction does not resolve whole rejected range");
    state.file_result("edit_file", "script.js", true, "SYNTAX_OK:", range(20, 29));
    check(!state.pending().empty(), "intervening mutation invalidates coordinates rather than clearing stale target");
    check(!state.abandon("script.js", "  "), "abandon requires an explicit reason");
    check(state.abandon("./script.js", "Оставляем этот подэтап на следующий ход"), "explicit abandonment resolves pending operation");
    check(state.pending().empty() && state.has_abandoned() && state.incomplete(), "abandoned task remains explicitly incomplete");
    check(!state.abandoned_summary().empty(), "abandoned files available for honest final result");
  }
  {
    arli::VerificationState state;
    state.file_result("edit_file", "script.js", false, "SYNTAX_REJECTED:", range(20, 29));
    state.file_result("edit_file", "other.js", true, "SYNTAX_OK:", range(20, 29));
    check(!state.pending().empty(), "other-file correction cannot clear rejected operation");
    state.file_result("edit_file", "./script.js", true, "SYNTAX_OK:", range(19, 30));
    check(state.pending().empty(), "successful covering-range correction resolves actual rejected operation");
  }
  {
    arli::VerificationState state;
    auto bad = range(20, 29), unrelated = range(1, 2);
    bad["old_text"] = unrelated["old_text"] = "obsolete field ignored by actual range dispatch";
    state.file_result("edit_file", "script.js", false, "SYNTAX_REJECTED:", bad);
    state.file_result("edit_file", "script.js", true, "SYNTAX_OK:", unrelated);
    check(!state.pending().empty(), "target matches actual dispatcher precedence when legacy arguments mix range and old_text");
  }
  {
    arli::VerificationState state;
    state.file_result("replace_text", "script.js", false, "SYNTAX_REJECTED:", {{"old_text", "first target"}, {"search", "stray legacy field"}});
    state.file_result("replace_text", "script.js", true, "SYNTAX_OK:", {{"old_text", "unrelated target"}, {"search", "stray legacy field"}});
    check(!state.pending().empty(), "strict replace ignores stray search field when identifying rejected operation");
    state.file_result("replace_text", "script.js", true, "SYNTAX_OK:", {{"old_text", "first target"}, {"search", "another stray field"}});
    check(state.pending().empty(), "strict replace correction matches actual old_text regardless of ignored aliases");
  }
  {
    arli::VerificationState state;
    const json expected = {{"old_text", "const secret = 'private-source';\r\n"}, {"new_text", "private replacement"}};
    state.file_result("replace_text", "module.js", false, "READ_REQUIRED:", expected);
    state.file_result("replace_text", "module.js", true, "SYNTAX_OK:", {{"old_text", "different function"}});
    state.file_result("check_file", "module.js", true, "SYNTAX_OK:");
    check(!state.pending().empty(), "unrelated same-file write and syntax check cannot clear rejected text target");
    const auto serialized = state.serialized();
    check(serialized.find("private-source") == std::string::npos && serialized.find("private replacement") == std::string::npos,
          "durable pending state contains neither original source nor replacement");
    arli::VerificationState restored;
    check(restored.restore(serialized) && restored.pending() == state.pending(), "pending operation survives continuation/restart");
    restored.file_result("replace_text", "module.js", true, "SYNTAX_UNCHECKED:", {{"old_text", "const secret = 'private-source';\n"}});
    check(restored.pending().empty(), "successful same-old-text correction resolves preflight failure using normalized newline fingerprint");
  }
  {
    arli::VerificationState state;
    state.file_result("append_file", "note.txt", false, "READ_REQUIRED:", {{"content", "intended\r\n"}});
    state.file_result("append_file", "note.txt", true, "SYNTAX_UNCHECKED:", {{"content", "different\r\n"}});
    state.file_result("append_file", "note.txt", true, "SYNTAX_UNCHECKED:", {{"content", "intended\n"}});
    check(!state.pending().empty(), "different appended bytes cannot resolve rejected append, including newline differences");
    arli::VerificationState resumed;
    check(resumed.restore(state.serialized()), "typed append target survives continuation");
    resumed.file_result("write_file", "note.txt", true, "SYNTAX_UNCHECKED:", {{"append", true}, {"text", "intended\r\n"}});
    check(resumed.pending().empty(), "fresh legitimate append retry, including legacy alias, clears corresponding preflight rejection");
    state.file_result("write_file", "created.json", false, "SYNTAX_REJECTED:", {{"content", "{broken"}});
    state.file_result("create_file", "created.json", true, "SYNTAX_OK:", {{"content", "{}"}});
    check(json::parse(state.serialized())["targets"].size() == 1, "write_file create-only alias shares actual create target semantics");
  }
  for (const char* code : {"TEXT_MISMATCH:", "TEXT_AMBIGUOUS:"}) {
    arli::VerificationState state;
    const json args = {{"old_text", "requested fragment"}, {"new_text", "replacement"}};
    state.file_result("replace_text", "config.json", false, code, args);
    state.file_result("check_file", "config.json", true, "SYNTAX_OK:");
    state.file_result("replace_text", "config.json", true, "SYNTAX_OK:", {{"old_text", "unrelated fragment"}});
    check(!state.pending().empty(), "rejected missing/ambiguous text target cannot be hidden by unrelated same-file edit");
    state.file_result("replace_text", "config.json", true, "SYNTAX_OK:", args);
    check(state.pending().empty(), "successful retry of same text target resolves missing/ambiguous preflight failure");
  }
  {
    arli::VerificationState state;
    state.file_result("edit_file", "old.js", false, "SYNTAX_REJECTED:");
    state.file_result("check_file", "old.js", true, "SYNTAX_OK:");
    state.file_result("edit_file", "old.js", true, "SYNTAX_OK:");
    check(!state.pending().empty(), "legacy missing target stays unresolved conservatively");
    check(state.abandon("old.js", "reason with sensitive data"), "legacy target can be explicitly abandoned");
    const auto encoded = state.serialized();
    check(encoded.find("sensitive") == std::string::npos, "abandon reason never persisted as raw data");
    arli::VerificationState restored;
    check(restored.restore(encoded) && restored.has_abandoned() && restored.pending().empty(), "abandoned status survives restart without claiming completed");
  }
  {
    arli::VerificationState state;
    state.file_result("check_file", "broken.js", false, "parse failure");
    check(!state.pending().empty(), "actual current-file syntax error remains tracked");
    state.file_result("edit_file", "broken.js", true, "SYNTAX_UNCHECKED:", range(1, 2));
    check(!state.pending().empty(), "unchecked mutation cannot claim existing syntax error repaired");
    arli::VerificationState restored;
    check(restored.restore(state.serialized()) && !restored.pending().empty(), "actual source error also survives continuation");
    restored.file_result("check_file", "broken.js", true, "SYNTAX_OK:");
    check(restored.pending().empty(), "successful current-file check resolves only current-file error");
    check(!restored.restore("{broken") && !restored.pending().empty(), "malformed durable state fails closed");
  }
  {
    arli::VerificationState state;
    for (int i = 0; i < 40; ++i) state.file_result("edit_file", "file" + std::to_string(i), false, "SYNTAX_REJECTED:", range(1, 2));
    check(json::parse(state.serialized())["targets"].size() == 32, "rejected target tracking is bounded");
    for (int i = 0; i < 40; ++i) state.abandon("file" + std::to_string(i), "defer");
    check(!state.pending().empty(), "overflow never silently drops incomplete operations");
  }
  {
    arli::VerificationState state;
    state.observe("node stage-test.cjs", false, "operator declined");
    state.observe("node independent-test.cjs", false, "exit 1");
    state.mutation();
    state.defer_checks({"node stage-test.cjs"});
    check(!state.blocked() && state.pending() == "node independent-test.cjs" && state.due() == "node independent-test.cjs",
          "explicit stage deferral retires only that stage's failed/blocked commands");
    state.file_result("edit_file", "still-unapplied.js", false, "SYNTAX_REJECTED:", range(1, 2));
    state.defer_checks({"node independent-test.cjs"});
    check(state.due().empty() && !state.pending().empty(), "stage deferral never clears rejected file operations");
  }
  {
    arli::VerificationState state;
    for (int i = 0; i < 32; ++i)
      state.file_result("edit_file", std::string(4000, '\\') + std::to_string(i), false, "SYNTAX_REJECTED:", range(1, 2));
    const auto encoded = state.serialized();
    check(json(encoded).dump().size() <= 28000, "durable verification state respects nested workflow byte budget");
    arli::VerificationState restored;
    check(restored.restore(encoded) && !restored.pending().empty() && json::parse(encoded)["overflow"] == true,
          "oversized durable state is explicit fail-closed overflow rather than dropped success");
  }
  return failures ? 1 : 0;
}
