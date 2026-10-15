#pragma once
#include "../tools.hpp"
namespace arli {
// Anchored publication. expected detects changes before commit, not a filesystem CAS.
ToolResult publish_file(const std::filesystem::path& file, const std::string& content,
                        bool replace, const std::string* expected = nullptr,
                        const std::filesystem::path* metadata_source = nullptr);
}
