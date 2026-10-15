#include "read_edit_guard.hpp"
#include "tools/backup_store.hpp"
#include "turn_policy.hpp"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; } }
std::string read(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
void seed(const fs::path& path, const std::string& source) { std::ofstream(path, std::ios::binary) << source; }

int main() {
  char tmp[] = "/tmp/arli-strict-replace-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path wd(tmp);
  arli::ReadEditGuard gate;
  auto edit = [&](const json& args) { return gate.edit(wd, "replace_text", args.dump()); };
  auto seen = [&](const std::string& path, int from = 0, int to = 0) {
    const auto result = arli::tool_read_file(wd, path, 120000, from, to);
    check(result.ok, "source read succeeds"); gate.record(wd, path, result.output);
  };
  const std::string original = "const first = 1;\r\nconst second = 2;\r\nlast";
  seed(wd / "source.txt", original);
  const json args{{"path", "source.txt"}, {"old_text", "const second = 2;"}, {"new_text", "const second = 3;"}};
  check(!edit(args).ok && read(wd / "source.txt") == original, "unread replacement cannot write");
  seen("source.txt", 1, 1);
  check(!edit(args).ok, "reading another range does not authorize matched lines");
  seen("source.txt", 2, 2);
  for (const auto* key : {"path", "old_text", "new_text"}) {
    auto invalid = args; invalid.erase(key);
    check(!edit(invalid).ok, "missing required strict argument rejected");
    for (const auto& value : json::array({nullptr, 1, false, json::array(), json::object()})) {
      invalid = args; invalid[key] = value;
      check(!edit(invalid).ok, "nonstrings never coerce to deletion or source");
    }
  }
  auto alias = args; alias.erase("path"); alias["file"] = "source.txt";
  check(!edit(alias).ok, "strict tool requires explicit canonical path");
  auto empty = args; empty["old_text"] = "";
  check(!edit(empty).ok, "empty old_text cannot insert or replace arbitrary bytes");
  auto fuzzy = args; fuzzy["old_text"] = "  const second = 2;";
  check(!edit(fuzzy).ok && read(wd / "source.txt") == original, "strict replacement never falls back to indentation matching");
  check(!arli::dispatch_tool(wd, "replace_text", args.dump(), {}, true).ok,
        "read-only policy rejects strict replacement");
  check(!arli::dispatch_tool(wd, "replace_text", args.dump()).ok,
        "stateless dispatch cannot bypass fresh-read guard");
  check(edit(args).ok, "fresh unique fragment edits successfully");
  check(read(wd / "source.txt") == "const first = 1;\r\nconst second = 3;\r\nlast",
        "strict fragment edit preserves CRLF and unselected bytes");
  check(read(arli::tools_internal::backup_path(wd, wd / "source.txt")) == original, "strict edit preserves before snapshot");
  auto next = args; next["old_text"] = "const second = 3;"; next["new_text"] = "const second = 4;";
  check(!edit(next).ok, "successful strict write consumes read permission");
  seen("source.txt");
  const std::string external = "external change\n" + read(wd / "source.txt");
  seed(wd / "source.txt", external);
  check(!edit(next).ok && read(wd / "source.txt") == external, "external change invalidates old snapshot");
  check(!arli::tool_replace_text(wd, "source.txt", "last", "tail", original).ok,
        "low-level strict publish rejects stale expected content");
  seed(wd / "repeated.txt", "alpha\nalpha\n"); seen("repeated.txt");
  check(!edit(json{{"path", "repeated.txt"}, {"old_text", "alpha"}, {"new_text", "beta"}}).ok,
        "multiple exact matches rejected");
  seed(wd / "overlap.txt", "aaa"); seen("overlap.txt");
  check(!edit(json{{"path", "overlap.txt"}, {"old_text", "aa"}, {"new_text", "b"}}).ok,
        "overlapping exact matches are ambiguous");
  seed(wd / "multiline.txt", "before\r\nold\r\nblock\r\nafter"); seen("multiline.txt", 2, 2);
  const json multi{{"path", "multiline.txt"}, {"old_text", "old\nblock\n"}, {"new_text", "new\nblock\n"}};
  check(!edit(multi).ok, "entire old fragment must have been read");
  seen("multiline.txt", 3, 3);
  check(edit(multi).ok && read(wd / "multiline.txt") == "before\r\nnew\r\nblock\r\nafter",
        "LF from read_file matches CRLF source without altering surrounding bytes");
  seen("multiline.txt");
  check(edit(json{{"path", "multiline.txt"}, {"old_text", "new\nblock\n"}, {"new_text", ""}}).ok &&
        read(wd / "multiline.txt") == "before\r\nafter", "explicit empty replacement deletes only unique fragment");
  seed(wd / "data.json", "{\"n\":1}\n"); seen("data.json");
  const auto rejected = edit(json{{"path", "data.json"}, {"old_text", "1"}, {"new_text", "broken"}});
  check(!rejected.ok && rejected.output.find("SYNTAX_REJECTED") != std::string::npos &&
        read(wd / "data.json") == "{\"n\":1}\n" && !fs::exists(arli::tools_internal::backup_path(wd, wd / "data.json")),
        "strict edit runs assembled candidate syntax check before mutation or backup");
  check(edit(json{{"path", "data.json"}, {"old_text", "1"}, {"new_text", "2"}}).ok,
        "syntax rejection preserves permission for corrected candidate on same snapshot");
  check(arli::is_mutation_tool("replace_text") && arli::tool_writes_workdir("replace_text"), "strict tool participates in all mutation gates");
  const auto schema = json::parse(arli::tools_openai_schema_json()); bool advertised = false;
  for (const auto& tool : schema) if (tool["function"]["name"] == "replace_text") {
    advertised = tool["function"]["parameters"]["required"] == json::array({"path", "old_text", "new_text"});
  }
  check(advertised, "strict contract advertised to model");
  seed(wd / "first.json", "{\"a\":1}\n"); seed(wd / "second.json", "{\"b\":2}\n");
  seen("first.json"); seen("second.json");
  const json batch{{"edits", json::array({
      json{{"path", "first.json"}, {"old_text", "1"}, {"new_text", "3"}},
      json{{"path", "second.json"}, {"old_text", "2"}, {"new_text", "4"}}
  })}};
  check(gate.batch_replace(wd, batch.dump()).ok && read(wd / "first.json") == "{\"a\":3}\n" &&
        read(wd / "second.json") == "{\"b\":4}\n", "checked batch updates every file");
  seen("first.json"); seen("second.json");
  auto mismatch = batch; mismatch["edits"][0]["old_text"] = "3"; mismatch["edits"][0]["new_text"] = "5";
  mismatch["edits"][1]["old_text"] = "missing";
  check(!gate.batch_replace(wd, mismatch.dump()).ok && read(wd / "first.json") == "{\"a\":3}\n" &&
        read(wd / "second.json") == "{\"b\":4}\n", "batch mismatch writes no files");
  seed(wd / "second.json", "{\"b\":40}\n");
  auto stale = batch; stale["edits"][0]["old_text"] = "3"; stale["edits"][0]["new_text"] = "5";
  stale["edits"][1]["old_text"] = "4"; stale["edits"][1]["new_text"] = "6";
  check(!gate.batch_replace(wd, stale.dump()).ok && read(wd / "first.json") == "{\"a\":3}\n" &&
        read(wd / "second.json") == "{\"b\":40}\n", "stale later file prevents earlier batch write");
  seed(wd / "second.json", "{\"b\":4}\n"); seen("first.json"); seen("second.json");
  auto bad_syntax = batch; bad_syntax["edits"][0]["old_text"] = "3"; bad_syntax["edits"][0]["new_text"] = "broken";
  bad_syntax["edits"][1]["old_text"] = "4"; bad_syntax["edits"][1]["new_text"] = "5";
  check(!gate.batch_replace(wd, bad_syntax.dump()).ok && read(wd / "first.json") == "{\"a\":3}\n" &&
        read(wd / "second.json") == "{\"b\":4}\n", "batch syntax rejection writes no files");
  fs::create_directory(wd / "locked"); seed(wd / "locked/value.txt", "old\n");
  seen("first.json"); seen("locked/value.txt"); fs::permissions(wd / "locked", fs::perms::owner_read|fs::perms::owner_exec);
  const json rollback{{"edits",json::array({
      json{{"path","first.json"},{"old_text","3"},{"new_text","7"}},
      json{{"path","locked/value.txt"},{"old_text","old"},{"new_text","new"}}
  })}};
  const auto rolled=gate.batch_replace(wd,rollback.dump());
  fs::permissions(wd / "locked",fs::perms::owner_all);
  check(!rolled.ok && rolled.output.find("BATCH_ROLLED_BACK")!=std::string::npos &&
        read(wd / "first.json")=="{\"a\":3}\n" && read(wd / "locked/value.txt")=="old\n",
        "publication failure rolls back earlier files");
  check(arli::is_mutation_tool("batch_replace") && arli::tool_writes_workdir("batch_replace"),
        "batch participates in mutation policy");
  bool batch_advertised = false;
  for (const auto& tool : schema) if (tool["function"]["name"] == "batch_replace") batch_advertised = true;
  check(batch_advertised, "batch contract advertised to model");
  fs::remove_all(wd);
  return failures ? 1 : 0;
}
