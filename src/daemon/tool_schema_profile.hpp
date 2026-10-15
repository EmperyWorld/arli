#pragma once

#include "llm_models.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace arli {
inline void filter_tool_schemas(nlohmann::json& tools, const std::string& profile) {
  if (normalize_llm_profile(profile) != "qwen3" || !tools.is_array()) return;
  tools.erase(std::remove_if(tools.begin(), tools.end(), [](const auto& schema) {
    return schema.value("function", nlohmann::json::object()).value("name", "") == "edit_file";
  }), tools.end());
}
}
