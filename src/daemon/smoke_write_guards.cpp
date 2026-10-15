#include "tools/backup_store.hpp"
#include "paths.hpp"
#include "tools.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL %s\n", msg);
  return 1;
}

int main() {
  setenv("ARLI_STATE_DIR", "/tmp/arli-write-guards", 1);
  namespace fs = std::filesystem;
  const fs::path wd = arli::state_root() / "workspace";
  fs::remove_all(arli::state_root());
  fs::create_directories(wd);

  {
    std::string body(2000, 'a');
    body = "<html>\n" + body + "\n</html>\n";
    auto w = arli::tool_create_file(wd, "page.html", body);
    if (!w.ok) return fail("seed create");
  }

  {
    auto r = arli::tool_create_file(wd, "page.html", "x");
    if (r.ok) return fail("create overwrite allowed");
    std::printf("OK create existing blocked: %s\n", r.output.substr(0, 80).c_str());
  }

  {
    auto r = arli::dispatch_tool(wd, "write_file",
                                 R"({"path":"page.html","content":"<html>stamp</html>","replace":true})");
    if (r.ok) return fail("legacy write_file stamp allowed");
    std::printf("OK legacy write→create blocked\n");
  }

  {
    auto r = arli::tool_edit_file(wd, "page.html", "</html>\n", "<!-- edited -->\n</html>\n");
    if (!r.ok) return fail(("edit failed: " + r.output).c_str());
    std::printf("OK edit: %s\n", r.output.substr(0, 120).c_str());
    if (!fs::exists(arli::tools_internal::backup_path(wd, wd / "page.html"))) return fail("no backup");
    std::printf("OK backup exists\n");
  }

  // Fuzzy: trailing spaces in old_text.
  {
    auto r = arli::tool_edit_file(wd, "page.html", "<!-- edited -->   \n", "<!-- fuzzy -->\n");
    if (!r.ok) return fail(("fuzzy edit failed: " + r.output).c_str());
    if (r.output.find("fuzzy_ws") == std::string::npos && r.output.find("fuzzy_") == std::string::npos)
      return fail("expected fuzzy marker");
    std::printf("OK fuzzy trailing-ws edit\n");
  }

  // Tabs in file, spaces in old_text (Claude-style).
  {
    auto c = arli::tool_create_file(wd, "tabs.js", "function hi() {\n\treturn 1;\n}\n");
    if (!c.ok) return fail("tabs create");
    auto r = arli::tool_edit_file(wd, "tabs.js", "function hi() {\n    return 1;\n}\n",
                                  "function hi() {\n    return 2;\n}\n");
    if (!r.ok) return fail(("tab fuzzy failed: " + r.output).c_str());
    if (r.output.find("fuzzy_tab") == std::string::npos && r.output.find("fuzzy_indent") == std::string::npos)
      return fail(("expected fuzzy_tab/indent, got: " + r.output).c_str());
    auto read = arli::tool_read_file(wd, "tabs.js");
    if (read.output.find("return 2") == std::string::npos) return fail("tab edit content");
    std::printf("OK fuzzy tab/indent edit\n");
  }

  // Near-match hint on miss.
  {
    auto r = arli::tool_edit_file(wd, "tabs.js", "function hi() {\n    return 999;\n}\n", "x");
    if (r.ok) return fail("miss should fail");
    if (r.output.find("near L") == std::string::npos) return fail(("no near hint: " + r.output).c_str());
    std::printf("OK near-match hint: %s\n", r.output.substr(0, 100).c_str());
  }

  // Directory path must not open as file.
  {
    auto r = arli::tool_edit_file(wd, ".", "a", "b");
    if (r.ok) return fail("edit dir allowed");
    std::printf("OK dir edit blocked: %s\n", r.output.substr(0, 80).c_str());
  }
  {
    fs::create_directories(wd / "subdir");
    auto r = arli::tool_read_file(wd, "subdir");
    if (r.ok) return fail("read dir allowed");
    std::printf("OK dir read blocked: %s\n", r.output.substr(0, 80).c_str());
  }

  {
    auto r = arli::tool_create_file(wd, "new.txt", "hello\n");
    if (!r.ok) return fail("new file");
    auto tiny = arli::tool_append_file(wd, "new.txt", "x\n");
    if (!tiny.ok) return fail("small append rejected");
    const std::string chunk =
        "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8 world\n";
    r = arli::tool_append_file(wd, "new.txt", chunk);
    if (!r.ok) return fail(("append: " + r.output).c_str());
    auto read = arli::tool_read_file(wd, "new.txt");
    if (read.output.find("hello") == std::string::npos || read.output.find("world") == std::string::npos)
      return fail("append content");
    std::printf("OK create+append (small append supported)\n");
  }

  // Line-addressed edit (primary contract).
  {
    auto r = arli::tool_edit_lines(wd, "new.txt", 1, 1, "HELLO");
    if (!r.ok) return fail(("edit_lines failed: " + r.output).c_str());
    auto read = arli::tool_read_file(wd, "new.txt");
    if (read.output.find("1|HELLO") == std::string::npos) return fail(("numbered read: " + read.output).c_str());
    std::printf("OK edit_lines + numbered read\n");
  }
  {
    std::string body;
    for (int i = 0; i < 30; ++i) body += "L\n";
    auto c = arli::tool_create_file(wd, "wide.txt", body);
    if (!c.ok) return fail("wide seed");
    auto wide = arli::tool_edit_lines(wd, "wide.txt", 1, 30, "x\n");
    if (wide.ok) return fail("wide stub edit allowed");
    if (wide.output.find("пустой") == std::string::npos && wide.output.find("почти") == std::string::npos)
      return fail(("wide msg: " + wide.output).c_str());
    std::printf("OK wide stub blocked\n");
  }
  {
    std::string body;
    for (int i = 0; i < 50; ++i) body += "L\n";
    auto c = arli::tool_create_file(wd, "grow.txt", body);
    if (!c.ok) return fail("grow seed");
    std::string whole;
    for (int i = 0; i < 50; ++i) whole += "Z\n";
    // The requested edit must apply completely, including the tail.
    auto rewritten = arli::tool_edit_lines(wd, "grow.txt", 1, 50, whole);
    if (!rewritten.ok) return fail(("full range rewrite: " + rewritten.output).c_str());
    auto head = arli::tool_read_file(wd, "grow.txt", 120000, 1, 35);
    if (head.output.find("1|Z") == std::string::npos) return fail("range did not write Z");
    if (head.output.find("31|Z") == std::string::npos) return fail("tail of requested window not edited");
    auto last = arli::tool_read_file(wd, "grow.txt", 120000, 50, 50);
    if (last.output.find("50|Z") == std::string::npos) return fail("last line not edited");
    auto invalid = arli::tool_edit_lines(wd, "grow.txt", 1, 80, whole);
    if (invalid.ok) return fail("invalid range silently applied");
    auto after = arli::tool_read_file(wd, "grow.txt", 120000, 1, 50);
    if (after.output.find("50|Z") == std::string::npos) return fail("invalid range changed file");
    const std::string tail =
        "tail1\ntail2\ntail3\ntail4\ntail5\ntail6\ntail7\ntail8\n";
    auto grown = arli::tool_append_file(wd, "grow.txt", tail);
    if (!grown.ok) return fail(("grow append: " + grown.output).c_str());
    auto read = arli::tool_read_file(wd, "grow.txt", 120000, 49, 60);
    if (read.output.find("tail8") == std::string::npos) return fail(("not grown: " + read.output).c_str());
    std::printf("OK full range edited + invalid range rejected + append grows\n");
  }

  // Ambiguous old_text.
  {
    auto c = arli::tool_create_file(wd, "dup.txt", "alpha line\nbb\nalpha line\n");
    if (!c.ok) return fail("dup create");
    auto r = arli::tool_edit_file(wd, "dup.txt", "alpha line\n", "XX\n");
    if (r.ok) return fail("ambiguous edit allowed");
    std::printf("OK ambiguous blocked\n");
  }

  // Gemma stub old_text rejected before fuzzy thrash.
  {
    auto r = arli::tool_edit_file(wd, "page.html", "/**", "/*x*/");
    if (r.ok) return fail("stub /** allowed");
    if (r.output.find("короткий") == std::string::npos && r.output.find("общий") == std::string::npos)
      return fail(("expected weak old_text msg: " + r.output).c_str());
    std::printf("OK weak old_text blocked: %s\n", r.output.substr(0, 90).c_str());
  }

  // Text and version edits are legitimate file changes.
  {
    auto c = arli::tool_create_file(wd, "t.html",
                                    "<html><head><title>Arli OS v5.0: Desktop</title></head>"
                                    "<body><h1>Hi</h1></body></html>\n");
    if (!c.ok) return fail("t create");
    auto r = arli::tool_edit_file(wd, "t.html", "Arli OS v5.0: Desktop", "Arli OS v7.0: Multimedia");
    if (!r.ok) return fail("banner rename rejected");
    auto read = arli::tool_read_file(wd, "t.html");
    if (read.output.find("Arli OS v7.0") == std::string::npos) return fail("banner not changed");
    std::printf("OK banner edit\n");
    r = arli::tool_edit_file(wd, "t.html", "<h1>Hi</h1>", "<h1 class=\"hero\">Hi</h1>");
    if (!r.ok) return fail(("real edit blocked: " + r.output).c_str());
    std::printf("OK real markup edit\n");
  }

  {
    const auto before = arli::tool_read_file(wd, "new.txt").output;
    auto result = arli::dispatch_tool(wd, "edit_file", R"({"path":"new.txt","start_line":1,"end_line":1})");
    if (result.ok) return fail("missing new_text treated as deletion");
    if (arli::tool_read_file(wd, "new.txt").output != before) return fail("missing replacement changed file");
  }

  // Shell write bypass blocked (no confirm path).
  {
    if (!arli::shell_mutates_files("cat << 'EOF' > style.css\n:root{}\nEOF")) return fail("heredoc write not detected");
    if (!arli::shell_mutates_files("echo x >> f.txt")) return fail("append redirect not detected");
    if (!arli::shell_mutates_files("sed -i 's/a/b/' x.js")) return fail("sed -i not detected");
    if (arli::shell_mutates_files("ls -la")) return fail("ls false positive");
    if (arli::shell_mutates_files("git add .")) return fail("git add false positive");
    if (arli::shell_mutates_files("npm install express")) return fail("npm install false positive");
    if (arli::shell_mutates_files("scp a b")) return fail("scp false positive");
    auto r = arli::tool_run_shell(wd, "cat << 'EOF' > style.css\nx\nEOF");
    if (r.ok) return fail("shell write executed");
    std::printf("OK shell write blocked: %s\n", r.output.substr(0, 60).c_str());
  }

  std::printf("WRITE_GUARDS_OK\n");
  return 0;
}
