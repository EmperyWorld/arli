#include "mutation_diff.hpp"

#include <cstdio>
#include <string>

namespace {

int fail(const char* msg) {
  std::fprintf(stderr, "FAIL %s\n", msg);
  return 1;
}

#define LT_CHECK(cond, msg) \
  do {                    \
    if (!(cond)) return fail(msg); \
  } while (0)

}  // namespace

int main() {
  const std::vector<std::string> removed{"old"};
  const std::vector<std::string> added{"new"};
  const std::string d = arli::mutation_unified_diff("foo.txt", removed, added, 3);
  LT_CHECK(d.find("--- a/foo.txt") != std::string::npos, "header old");
  LT_CHECK(d.find("+++ b/foo.txt") != std::string::npos, "header new");
  LT_CHECK(d.find("@@ -3,1 +3,1 @@") != std::string::npos, "hunk");
  LT_CHECK(d.find("-old\n") != std::string::npos, "removed line");
  LT_CHECK(d.find("+new\n") != std::string::npos, "added line");

  arli::ToolResult r;
  arli::attach_mutation_diff(r, "x", removed, added);
  LT_CHECK(r.diff == arli::mutation_unified_diff("x", removed, added), "attach copies diff");
  std::printf("OK mutation-diff\n");
  return 0;
}
