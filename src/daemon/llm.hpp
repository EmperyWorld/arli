#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <string>

namespace arli {

struct LlmConfig {
  std::string base_url;
  std::string model;
  std::string api_key;
  // gemma4 | qwen3 | generic — see normalize_llm_profile().
  std::string profile = "gemma4";
  double temperature = -1.0; // -1: profile default; 0..2: explicit sampling.
  int context_tokens = 0; // 0 preserves direct-client defaults; production turns set the window.
  double prompt_token_scale = 1.0; // raised by actual provider usage within the turn

};

struct LlmStreamSink {
  std::function<bool(const std::string& delta)> on_token;
  std::function<bool(const std::string& delta)> on_thought;
};

struct LlmReply {
  bool ok = false;
  bool cancelled = false;
  int prompt_tokens = 0;
  int completion_tokens = 0;
  std::string error;
  std::string content;
  nlohmann::json message;  // full assistant message object
  nlohmann::json raw;
};

// tool_choice: "auto" | "required" | "none" (OpenAI Chat Completions).
// With `stream`, deltas are passed to sink; the returned message is fully assembled.
LlmReply chat_completions(const LlmConfig& cfg, const nlohmann::json& messages, const nlohmann::json& tools,
                          const std::string& tool_choice = "auto", std::atomic<bool>* cancel = nullptr,
                          LlmStreamSink* stream = nullptr);

}  // namespace arli
