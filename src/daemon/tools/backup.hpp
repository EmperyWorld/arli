#pragma once

#include "../tools.hpp"

namespace arli::tools_internal {

ToolResult backup_regular_file(const std::filesystem::path& workdir, const std::filesystem::path& file,
                               const std::string* expected = nullptr, bool update_latest = true);

}  // namespace arli::tools_internal
