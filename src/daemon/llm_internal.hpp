#pragma once

#include "llm.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace arli::llm_internal {

int request_prompt_tokens(const nlohmann::json& body, double scale = 1.0);
int response_reserve(const LlmConfig& cfg);
bool fit_chat_budget(nlohmann::json& messages, const LlmConfig& cfg, const nlohmann::json& tools);

std::string join_url(std::string base, const char* suffix);

nlohmann::json messages_for_gemma_template(nlohmann::json messages);

nlohmann::json build_chat_body(const LlmConfig& cfg, const nlohmann::json& messages, const nlohmann::json& tools,
                               const std::string& tool_choice, bool stream, const std::string& profile);

}  // namespace arli::llm_internal
