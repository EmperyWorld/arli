#include "mutation_diff.hpp"

#include <sstream>

namespace arli {
namespace {

constexpr int kMaxDiffLines = 240;

}  // namespace

std::string mutation_unified_diff(const std::string& path, const std::vector<std::string>& removed,
                                  const std::vector<std::string>& added, int old_start_line) {
  if (removed.empty() && added.empty()) {
    return {};
  }
  const int old_n = static_cast<int>(removed.size());
  const int new_n = static_cast<int>(added.size());
  const int start = old_start_line < 1 ? 1 : old_start_line;
  std::ostringstream out;
  out << "--- a/" << path << "\n+++ b/" << path << "\n@@ -" << start << "," << old_n << " +" << start << ","
      << new_n << " @@\n";
  int lines = 0;
  for (const auto& l : removed) {
    if (++lines > kMaxDiffLines) {
      out << "… diff truncated\n";
      break;
    }
    out << '-' << l << '\n';
  }
  for (const auto& l : added) {
    if (++lines > kMaxDiffLines) {
      out << "… diff truncated\n";
      break;
    }
    out << '+' << l << '\n';
  }
  return out.str();
}

void attach_mutation_diff(ToolResult& result, const std::string& path, const std::vector<std::string>& removed,
                          const std::vector<std::string>& added, int old_start_line) {
  result.diff = mutation_unified_diff(path, removed, added, old_start_line);
}

}  // namespace arli
