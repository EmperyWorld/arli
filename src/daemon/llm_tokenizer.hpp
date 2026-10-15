#pragma once
#include "llm.hpp"
#include <nlohmann/json.hpp>
#include <optional>

namespace arli::llm_internal {
// llama.cpp extension: render the configured chat template, then tokenize it.
// Returns nullopt for non-loopback providers or any unsupported/error response.
std::optional<int> exact_prompt_tokens(const LlmConfig& cfg, const nlohmann::json& messages,
                                       const nlohmann::json& tools);
}
