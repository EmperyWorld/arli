#include "llm_internal.hpp"
#include "context.hpp"
#include "tools.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
  using json = nlohmann::json;
  arli::LlmConfig cfg; cfg.model = "test"; cfg.profile = "gemma4"; cfg.context_tokens = 16384;
  const auto tools = json::parse(arli::tools_openai_schema_json());
  json messages = json::array({json{{"role","system"},{"content","PINNED: keep all constraints intact"}}});
  const auto body = arli::llm_internal::build_chat_body(cfg, messages, tools, "auto", true, cfg.profile);
  CHECK(arli::llm_internal::request_prompt_tokens(body) > arli::context::estimate_tokens(messages));
  CHECK(body["max_tokens"] == 4096 && body["reasoning_budget_tokens"] == 2048);
  for (int i = 0; i < 40; ++i) messages.push_back({{"role", i % 2 ? "assistant" : "user"},{"content",std::string(1200, 'x')}});
  CHECK(arli::llm_internal::fit_chat_budget(messages, cfg, tools));
  CHECK(messages[0]["content"] == "PINNED: keep all constraints intact");
  CHECK(arli::llm_internal::request_prompt_tokens(arli::llm_internal::build_chat_body(cfg,messages,tools,"auto",true,cfg.profile)) +
        arli::llm_internal::response_reserve(cfg) <= cfg.context_tokens);
  json impossible = json::array({json{{"role","system"},{"content",std::string(100000, 'p')}}});
  const auto original = impossible;
  CHECK(!arli::llm_internal::fit_chat_budget(impossible,cfg,tools));
  CHECK(impossible == original); // never silently truncate immutable instructions
  cfg.context_tokens = 1024;
  CHECK(!arli::llm_internal::fit_chat_budget(messages,cfg,tools));
  cfg.base_url = "http://127.0.0.1:1"; // must reject before attempting any network connection
  const auto reply = arli::chat_completions(cfg, messages, tools);
  CHECK(!reply.ok && reply.error.rfind("CONTEXT_LIMIT",0) == 0);
  cfg.context_tokens = 16384; cfg.prompt_token_scale = 2;
  CHECK(arli::llm_internal::request_prompt_tokens(body,2) >= 2*arli::llm_internal::request_prompt_tokens(body)-1);
  std::cout << "OK request-budget\n";
}
