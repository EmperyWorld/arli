#include "tools/backup_store.hpp"
// arli-test-tools — Tier A/B smoke (no LLM).
#include "tools/http_tools.hpp"
#include "tools/tier_a.hpp"
#include "tools.hpp"
#include "read_edit_guard.hpp"
#include "tools/shell_run.hpp"
#include "audit.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  const fs::path wd = fs::temp_directory_path() / "arli-tools-test";
  std::error_code ec;
  fs::remove_all(wd, ec);
  fs::create_directories(wd);
  fs::path sub = wd / "src";
  fs::create_directories(sub);
  {
    std::ofstream(sub / "a.cpp") << "line1\nline2\n";
    std::ofstream(sub / "b.txt") << "x";
  }

  for (const auto& sample : std::vector<std::pair<std::string,int>>{{"",0},{"ready",1},{"one\r\ntwo",2},{"one\ntwo\n",2},{std::string(2000,'\n'),2000}}) {
    std::ofstream(wd / "stats.txt",std::ios::binary) << sample.first;
    const auto stats=arli::tool_file_stat(wd,"stats.txt");
    if(!stats.ok||stats.output.find("\nlines="+std::to_string(sample.second)+"\n")==std::string::npos) return fail("file_stat counts raw lines, not formatted read output");
  }
  fs::remove(wd / "stats.txt");
  auto glob = arli::tool_glob_files(wd, "*.cpp", "src", {});
  if (!glob.ok || glob.output.find("a.cpp") == std::string::npos) {
    return fail("glob_files");
  }

  auto stat = arli::tool_file_stat(wd, "src/a.cpp");
  if (!stat.ok || stat.output.find("type=file") == std::string::npos) {
    return fail("file_stat");
  }

  auto search = arli::tool_search_files(wd, "a.cpp", ".", {});
  if (!search.ok || search.output.find("a.cpp") == std::string::npos) {
    return fail("search_files");
  }

  auto sum = arli::tool_summarize_dir(wd, "src", {});
  if (!sum.ok || sum.output.find("files=") == std::string::npos) {
    return fail("summarize_dir");
  }

  auto quote = arli::tool_quote_file(wd, "src/a.cpp", 1, 1);
  if (!quote.ok || quote.output.find("1|") == std::string::npos) {
    return fail("quote_file");
  }

  if (!arli::tool_make_dir(wd, "nested/deep").ok) {
    return fail("make_dir");
  }

  if (!arli::tool_copy_file(wd, "src/a.cpp", "src/a_copy.cpp").ok) {
    return fail("copy_file");
  }

  if (!arli::tool_move_file(wd, "src/a_copy.cpp", "src/moved.cpp").ok) {
    return fail("move_file");
  }

  if (!arli::tool_delete_file(wd, "src/moved.cpp").ok) {
    return fail("delete_file");
  }

  if (!arli::tool_restore_file(wd, "src/moved.cpp").ok) {
    return fail("restore_file");
  }

  auto blocked = arli::tool_http_get("http://127.0.0.1:8080/");
  if (blocked.ok) {
    return fail("http_get should block loopback");
  }
  if(arli::dispatch_tool(wd,"http_get",R"({"url":"https://example.com"})",{},true).ok)
    return fail("read-only mode blocks even GET because remote endpoints can have side effects");
  std::string pinned,why;
  if(!arli::pin_public_addresses("example.com","443",{"8.8.8.8","2606:4700:4700::1111","8.8.8.8"},pinned,why)||
     pinned!="example.com:443:8.8.8.8,[2606:4700:4700::1111]")return fail("dual-stack DNS pinning");
  pinned.clear();why.clear();
  if(arli::pin_public_addresses("example.com","443",{"8.8.8.8","127.0.0.1"},pinned,why)||
     why.find("blocked")==std::string::npos)return fail("mixed public/private DNS answer must fail closed");

  const auto shell = arli::tool_run_shell(wd, "test -f src/a.cpp && printf SHELL_OK");
  if (!shell.ok || shell.output != "SHELL_OK") return fail("sandbox shell keeps /tmp workspace and executes /bin/sh: " + shell.output);
  std::cout << "OK tools-tier\n";
  std::ofstream(wd / "source.js") << "const a = 1;\nconst b = 2;\n";
  {
    arli::ReadEditGuard gate;
    const auto args = nlohmann::json{{"path", "source.js"}, {"start_line", 1}, {"end_line", 1}, {"text", "const a = 3;"}}.dump();
    if (gate.edit(wd, "edit_file", args).ok) return fail("editing requires a read");
    if (gate.overwrite_gate(wd, "copy_file", R"({"src":"src/a.cpp","dest":"source.js"})").ok)
      return fail("copy cannot bypass reads when overwriting an existing file");
    if (gate.overwrite_gate(wd, "restore_file", R"({"path":"source.js"})").ok)
      return fail("restore cannot bypass reads when overwriting an existing file");
    auto shown = arli::tool_read_file(wd, "source.js", 120000, 1, 1);
    gate.record(wd, "source.js", shown.output);
    auto outside = nlohmann::json{{"path", "source.js"}, {"start_line", 2}, {"end_line", 2}, {"text", "bad"}}.dump();
    if (gate.edit(wd, "edit_file", outside).ok) return fail("unread range rejected");
    std::ofstream(wd / "source.js") << "const a = 1;\nconst b = 4;\n";
    if (gate.edit(wd, "edit_file", args).ok) return fail("external changes invalidate reads even outside selected range");
    shown = arli::tool_read_file(wd, "source.js", 120000, 1, 1);
    gate.record(wd, "source.js", shown.output.substr(0, shown.output.find("1|const") + 5));
    if (gate.edit(wd, "edit_file", args).ok) return fail("truncated source line does not authorize edits");
    gate.record(wd, "source.js", shown.output);
    if (!gate.edit(wd, "edit_file", args).ok) return fail("fresh read authorizes line replacement");
    if (gate.edit(wd, "edit_file", args).ok) return fail("successful write consumes read permission");
    fs::remove(arli::tools_internal::backup_path(wd, wd / "source.js"));
    std::ofstream(wd / "source.js") << "const a = 1;\nconst b = 2;\n";
  }
  const auto gutters = arli::tool_edit_lines(wd, "source.js", 1, 2, "27|const a = 3;\n28|const b = 4;");
  if (gutters.ok || fs::exists(arli::tools_internal::backup_path(wd, wd / "source.js"))) return fail("reject source gutters before backup/write");
  const auto protocol = arli::tool_edit_file(wd, "source.js", "const a = 1;",
      "<tool_call|><|channel>thoughts<channel|>garbage");
  if (protocol.ok || fs::exists(arli::tools_internal::backup_path(wd, wd / "source.js"))) return fail("reject observed protocol leakage before backup/write");
  if (arli::tool_edit_lines(wd, "source.js", 1, 2, "const a = 1;\nconst b = 2;").ok) return fail("reject no-op line edit");
  if (arli::tool_edit_file(wd, "source.js", "const a = 1;", "const a = 1;").ok) return fail("reject no-op text edit");
  if (arli::tool_create_file(wd, "new.js", "1|x\n2|y").ok || fs::exists(wd / "new.js")) return fail("reject gutters in create");
  if (arli::tool_append_file(wd, "source.js", "1|x\n2|y").ok) return fail("reject gutters in append");
  if (!arli::tool_create_file(wd, "numbered.txt", "1|x\n2|y").ok) return fail("allow deliberate numbered data");
  std::string large;
  for (int i = 0; i < 539; ++i) large += "// original line " + std::to_string(i) + "\n";
  std::ofstream(wd / "large.js") << large;
  const std::string replacement = "// first line changed\n" + large.substr(large.find('\n') + 1);
  const auto edit = arli::dispatch_tool(wd, "edit_file", nlohmann::json{{"path", "large.js"},
      {"start_line", 1}, {"end_line", 539}, {"text", replacement}}.dump());
  std::ifstream changed(wd / "large.js");
  const std::string actual((std::istreambuf_iterator<char>(changed)), {});
  if (!edit.ok || actual != replacement || !fs::exists(arli::tools_internal::backup_path(wd, wd / "large.js")))
    return fail("large text payload writes exact source with backup");
  const auto anchor = arli::dispatch_tool(wd, "edit_file", nlohmann::json{{"path", "large.js"},
      {"search", "// original line 537"}, {"text", "// anchored edit"}}.dump());
  if (!anchor.ok) return fail("exact anchor works after preceding line edits");
  const auto stale = arli::dispatch_tool(wd, "edit_file", nlohmann::json{{"path", "large.js"},
      {"search", "// original line 537"}, {"text", "// stale edit"}}.dump());
  if (stale.ok) return fail("stale anchor must never write at an old line offset");
  {
    std::string source;
    for (int n = 1; n <= 542; ++n) source += "// row " + std::to_string(n) + "\n";
    std::ofstream(wd / "window.js") << source;
    arli::ReadEditGuard gate;
    gate.record(wd, "window.js", arli::tool_read_file(wd, "window.js", 120000, 1, 542).output);
    const auto args = nlohmann::json{{"path", "window.js"}, {"start_line", 1}, {"end_line", 542}, {"text", "// changed\n" + source.substr(source.find('\n') + 1)}}.dump();
    const auto blocked = gate.edit(wd, "edit_file", args);
    if (blocked.ok || blocked.output.find("401-542") == std::string::npos || blocked.output.find("\"start_line\":401") == std::string::npos)
      return fail("large read cap must point to the exact missing tail rather than repeating the beginning");
    gate.record(wd, "window.js", arli::tool_read_file(wd, "window.js", 120000, 401, 542).output);
    if (!gate.edit(wd, "edit_file", args).ok) return fail("reading missing tail enables the fully read range");
  }
  arli::StepRow step;
  step.args_json = std::string(4095, 'x') + "✅" + std::string(100, 'x');
  try { arli::audit::step_json(step).dump(); }
  catch (...) { return fail("UTF-8 audit truncation split a character"); }
  return 0;
}
