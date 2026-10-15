#pragma once
#include "tools.hpp"

namespace arli {
struct SyntaxCheck {
  bool checked = false;
  bool ok = true;
  std::string detail;
};
// Parses source only. Never executes project code, imports, scripts or shell commands.
SyntaxCheck check_source_syntax(const std::filesystem::path& workdir, const std::string& path,
                                const std::string& content);
ToolResult tool_check_file(const std::filesystem::path& workdir, const std::string& path);
}
