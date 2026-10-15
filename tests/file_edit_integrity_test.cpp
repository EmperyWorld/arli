#include "read_edit_guard.hpp"
#include "tools/atomic_publish.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; } }
std::string read(const fs::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
void seed(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }
int main() {
  char tmp[] = "/tmp/arli-edit-integrity-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path wd(tmp);
  const std::string prefix = "keep\xc2\xa0space\r\n";
  seed(wd / "source.txt", prefix + "old value\r\nlast");
  chmod((wd / "source.txt").c_str(), 0750);
  check(arli::tool_edit_lines(wd, "source.txt", 2, 2, "new value").ok, "line edit succeeds");
  check(read(wd / "source.txt") == prefix + "new value\r\nlast", "line edit preserves NBSP, CRLF and missing EOF newline");
  struct stat st{}; stat((wd / "source.txt").c_str(), &st);
  check((st.st_mode & 0777) == 0750, "atomic write preserves permissions and executable bits");
  check(arli::tool_edit_lines(wd, "source.txt", 3, 3, "tail").ok, "EOF replacement");
  check(read(wd / "source.txt") == prefix + "new value\r\ntail", "EOF replacement preserves no final newline");
  check(arli::tool_edit_file(wd, "source.txt", "new value", "other value").ok, "search edit succeeds");
  check(read(wd / "source.txt") == prefix + "other value\r\ntail", "search edit preserves surrounding bytes");
  seed(wd / "fuzzy.txt", "  old value  \nfollowing\n");
  check(arli::tool_edit_file(wd, "fuzzy.txt", "old value", "new value").ok, "substring edit");
  seed(wd / "fuzzy.txt", "  old value  \nfollowing\n");
  check(arli::tool_edit_file(wd, "fuzzy.txt", "    old value", "new value").ok, "fuzzy line edit");
  check(read(wd / "fuzzy.txt") == "new value\nfollowing\n", "fuzzy replacement does not join following line");
  seed(wd / "mixed.txt", "one\r\ntwo\nthree\r\nfour");
  check(arli::tool_edit_lines(wd, "mixed.txt", 2, 2, "changed").ok, "mixed newlines edit");
  check(read(wd / "mixed.txt") == "one\r\nchanged\r\nthree\r\nfour", "unselected mixed newline bytes preserved");
  const auto exact = arli::tool_read_file(wd, "source.txt");
  check(exact.ok && exact.output.find("final_newline=no") != std::string::npos,
        "read exposes missing final newline for byte-exact verification");
  seed(wd / "empty.txt", "");
  arli::ReadEditGuard gate;
  const auto append = json{{"path", "empty.txt"}, {"content", "first"}}.dump();
  check(!gate.edit(wd, "append_file", append).ok, "unread empty file is guarded");
  const auto empty = arli::tool_read_file(wd, "empty.txt");
  check(empty.ok, "empty file read succeeds"); gate.record(wd, "empty.txt", empty.output);
  check(gate.edit(wd, "append_file", append).ok && read(wd / "empty.txt") == "first", "read empty file can be filled");
  std::string large;
  for (int i = 1; i <= 2000; ++i) large += std::string(100, 'x') + std::to_string(i) + "\n";
  seed(wd / "large.txt", large);
  const auto tail = arli::tool_read_file(wd, "large.txt", 120000, 1999, 2000);
  check(tail.ok && tail.output.find("2000|") != std::string::npos, "read beyond original 120KB prefix");
  gate.record(wd, "large.txt", tail.output);
  check(gate.edit(wd, "edit_file", json{{"path", "large.txt"}, {"start_line", 2000}, {"end_line", 2000}, {"text", "fixed"}}.dump()).ok,
        "far range editable after read");
  check(read(wd / "large.txt") == large.substr(0, large.rfind('\n', large.size() - 2) + 1) + "fixed\n", "far edit preserves prefix");
  const auto bounded = arli::tool_read_file(wd, "large.txt", 220, 1, 400);
  check(bounded.ok && bounded.output.find("of 2000") != std::string::npos && bounded.output.find("start_line=3") != std::string::npos,
        "byte cap applies to displayed range with honest total and continuation");
  check(!arli::tool_read_file(wd, "large.txt", 2, 1, 1).ok, "oversized line returns actionable failure");
  seed(wd / "victim.txt", "unchanged");
  fs::create_symlink(wd / "victim.txt", wd / ".source.txt.arli-tmp");
  check(arli::tool_append_file(wd, "source.txt", "!").ok && read(wd / "victim.txt") == "unchanged", "predictable temporary symlink never followed");
  check(!arli::publish_file(wd / "victim.txt", "overwrite", false).ok && read(wd / "victim.txt") == "unchanged", "exclusive publish never overwrites existing file");
  fs::remove_all(wd);
  return failures ? 1 : 0;
}
