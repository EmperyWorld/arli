#pragma once

#include "tools.hpp"

#include <string>
#include <vector>

namespace arli {

// Unified diff for Inspector (---/+++/@@, +/- lines). Capped for storage/UI.
std::string mutation_unified_diff(const std::string& path, const std::vector<std::string>& removed,
                                  const std::vector<std::string>& added, int old_start_line = 1);

void attach_mutation_diff(ToolResult& result, const std::string& path, const std::vector<std::string>& removed,
                          const std::vector<std::string>& added, int old_start_line = 1);

}  // namespace arli
