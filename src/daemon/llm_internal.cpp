#include "llm_internal.hpp"

#include "llm_models.hpp"
#include "llm_structured.hpp"
#include "llm_sampling.hpp"
#include <algorithm>
#include <stdexcept>

namespace arli::llm_internal {

std::string join_url(std::string base, const char* suffix) {
  while (!base.empty() && base.back() == '/') {
    base.pop_back();
  }
  return base + suffix;
}

nlohmann::json messages_for_gemma_template(nlohmann::json messages) {
  if (!messages.is_array()) {
    return messages;
  }
  for (auto& msg : messages) {
    if (!msg.is_object() || !msg.contains("tool_calls") || !msg["tool_calls"].is_array()) {
      continue;
    }
    for (auto& call : msg["tool_calls"]) {
      if (!call.is_object() || !call.contains("function") || !call["function"].is_object()) {
        continue;
      }
      auto& fn = call["function"];
      if (!fn.contains("arguments") || !fn["arguments"].is_string()) {
        continue;
      }
      const std::string raw = fn["arguments"].get<std::string>();
      try {
        fn["arguments"] = nlohmann::json::parse(raw.empty() ? "{}" : raw);
      } catch (...) {
        fn["arguments"] = nlohmann::json::object();
      }
      if (!fn["arguments"].is_object()) {
        fn["arguments"] = nlohmann::json::object({{"value", fn["arguments"]}});
      }
    }
  }
  return messages;
}

nlohmann::json build_chat_body(const LlmConfig& cfg, const nlohmann::json& messages, const nlohmann::json& tools,
                               const std::string& tool_choice, bool stream, const std::string& profile) {
  if (!valid_llm_temperature(cfg.temperature)) throw std::invalid_argument("invalid llm temperature");
  const std::string prof = normalize_llm_profile(profile.empty() ? cfg.profile : profile);
  nlohmann::json wire_messages = messages;
  if (prof == "gemma4") {
    wire_messages = messages_for_gemma_template(messages);
  }
  nlohmann::json body = {{"model", cfg.model}, {"messages", std::move(wire_messages)}, {"temperature", 0.2},
                         {"max_tokens", 4096}};
  if (stream) {
    body["stream"] = true;
    if (cfg.context_tokens > 0) body["stream_options"] = {{"include_usage", true}};
  }
  if (prof == "gemma4") {
    body["chat_template_kwargs"] = {{"enable_thinking", true}, {"preserve_thinking", true}};
    // Match Google's Gemma 4 sampling. A tiny forced thought cutoff harms tool planning.
    body["temperature"] = 1.0;
    body["top_p"] = 0.95;
    body["top_k"] = 64;
    body["min_p"] = 0.0;
    body["repeat_penalty"] = 1.0;
    body["presence_penalty"] = 0.0;
    body["reasoning_budget_tokens"] = 2048;
    body["max_tokens"] = 8192;
  } else if (prof == "qwen3") {
    body["temperature"] = 1.0;
    body["top_p"] = 0.95;
    body["top_k"] = 20;
    body["min_p"] = 0.0;
    body["repeat_penalty"] = 1.0;
    body["presence_penalty"] = 0.0;
    body["reasoning_effort"] = "xhigh";
    body["reasoning_budget_tokens"] = 2048;
  }
  if (cfg.temperature >= 0.0) body["temperature"] = cfg.temperature;
  if (!tools.is_null() && tools.is_array() && !tools.empty()) {
    body["tools"] = tools;
    std::string choice = tool_choice.empty() ? "auto" : tool_choice;
    if (prof == "gemma4" && choice == "required") {
      choice = "auto";
    }
    body["tool_choice"] = choice;
    if (prof == "qwen3") body["parallel_tool_calls"] = false;
    if (prof == "gemma4") {
      body["parallel_tool_calls"] = false;
      if (choice != "none") {
        body["response_format"] = {{"type", "json_schema"}, {"json_schema", {
            {"name", "arli_action"}, {"strict", true}, {"schema", structured_tool_schema(tools)}}}};
        const std::string guide =
            "Выбери следующий шаг в JSON по заданной схеме. Для действия: "
            "{\"action\":\"tool\",\"name\":\"имя инструмента\",\"params\":{аргументы}}. "
            "Для итогового ответа: {\"action\":\"final\",\"answer\":\"текст\"}. "
            "Используй только реальные инструменты из tools. Выполняй задачу последовательно; "
            "итог выдавай после выполнения и проверки, либо объясни блокировку.";
        auto& wire = body["messages"];
        if (!wire.empty() && wire.front().value("role", "") == "system" && wire.front()["content"].is_string())
          wire.front()["content"] = wire.front()["content"].get<std::string>() + "\n" + guide;
        else wire.insert(wire.begin(), {{"role", "system"}, {"content", guide}});
      }
    }
  }
  if (cfg.context_tokens > 0) {
    body["max_tokens"] = response_reserve(cfg);
    if (body.contains("reasoning_budget_tokens"))
      body["reasoning_budget_tokens"] = std::min(2048, response_reserve(cfg) / 2);
  }
  return body;
}

}  // namespace arli::llm_internal
