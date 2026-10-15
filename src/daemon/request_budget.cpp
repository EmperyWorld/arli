#include "llm_internal.hpp"
#include "llm_models.hpp"
#include "context.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace arli::llm_internal {
int request_prompt_tokens(const nlohmann::json& body, double scale) {
  // Count the actual wire messages, tool schemas, structured response schema and
  // template guide. This is an estimate, not a model-specific tokenizer.
  nlohmann::json prompt = nlohmann::json::object();
  for (const auto* key : {"messages", "tools", "response_format"})
    if (body.contains(key)) prompt[key] = body[key];
  const double tokens = (static_cast<double>(prompt.dump().size()) / 4.0 + 256.0) * std::max(1.0, scale);
  return static_cast<int>(std::min(std::ceil(tokens), static_cast<double>(std::numeric_limits<int>::max())));
}
int response_reserve(const LlmConfig& cfg) {
  const auto profile = normalize_llm_profile(cfg.profile);
  const int normal = profile == "gemma4" ? 8192 : 4096;
  return cfg.context_tokens > 0 ? std::clamp(cfg.context_tokens / 4, 256, normal) : normal;
}
bool fit_chat_budget(nlohmann::json& messages, const LlmConfig& cfg, const nlohmann::json& tools) {
  if (cfg.context_tokens <= 0) return true;
  const auto overhead = build_chat_body(cfg, nlohmann::json::array(), tools, "auto", true, cfg.profile);
  const auto available = static_cast<std::int64_t>(cfg.context_tokens) - response_reserve(cfg) - request_prompt_tokens(overhead, cfg.prompt_token_scale);
  if (available <= 0) return false;
  // fit_budget aims at 80% of its argument; account for calibrated prompt cost.
  auto window = static_cast<int>(std::min(available / std::max(1.0, cfg.prompt_token_scale) * 1.25, 100000000.0));
  for (int pass = 0; pass < 10; ++pass) {
    context::fit_budget(messages, window);
    const auto total = static_cast<std::int64_t>(request_prompt_tokens(
        build_chat_body(cfg, messages, tools, "auto", true, cfg.profile), cfg.prompt_token_scale)) + response_reserve(cfg);
    if (total <= cfg.context_tokens) return true;
    if (window <= 1024) break;
    // The serialized template has overhead that the message-only compactor cannot
    // measure. Tighten its target using the complete request, preserving all pins.
    window = std::max(1024, window * 4 / 5);
  }
  return false;
}
}  // namespace arli::llm_internal
