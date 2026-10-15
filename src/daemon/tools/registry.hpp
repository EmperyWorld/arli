#pragma once

#include "tools.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace arli::tools_registry {

// Tier A/B tools added in Э5. Returns nullopt if name is not handled here.
std::optional<ToolResult> dispatch(const std::filesystem::path& workdir, const std::string& name,
                                   const nlohmann::json& args, const std::string& list_ignore, bool read_only);

void append_openai_schemas(nlohmann::json& tools);

}  // namespace arli::tools_registry
