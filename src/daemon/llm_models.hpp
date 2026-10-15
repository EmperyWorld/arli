#pragma once

#include "llm.hpp"

#include <string>

namespace arli {

struct SettingsRow;

// gemma4 (structured Gemma) | qwen3 (native Qwen reasoning/tools) | generic (plain OpenAI chat).
std::string normalize_llm_profile(std::string profile);

struct LlmProbeResult {
  bool reachable = false;
  bool model_listed = false;
  std::string error;
};

// GET {base}/models — short timeout, no streaming.
LlmProbeResult probe_llm_endpoint(const LlmConfig& cfg, const std::string& model_id);

LlmConfig llm_config_from_settings(const SettingsRow& s);

bool llm_health_ok(const SettingsRow& s);

}  // namespace arli
