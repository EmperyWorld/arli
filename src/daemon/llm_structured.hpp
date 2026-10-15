#pragma once
#include "llm.hpp"
namespace arli {
nlohmann::json structured_tool_schema(const nlohmann::json& tools);
void decode_structured_tool_reply(LlmReply& reply, const nlohmann::json& tools);
}
