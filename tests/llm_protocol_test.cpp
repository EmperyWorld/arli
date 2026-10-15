#include "llm_sse.hpp"
#include "llm_internal.hpp"
#include "llm_tool_format.hpp"
#include "llm_structured.hpp"
#include "tool_schema_profile.hpp"
#include "tools.hpp"
#include <cstdio>
#include <limits>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)
int main() {
  CHECK(arli::unparsed_tool_tail("Изучу файлы.\n<call:list_dir{path:<|\"|>.<|\"|>}/>\n"));
  CHECK(arli::unparsed_tool_tail("<|tool_call>call:list_dir{}<tool_call|>"));
  CHECK(!arli::unparsed_tool_tail("Пример: `<call:list_dir{}/>`."));
  CHECK(!arli::unparsed_tool_tail("Обычный ответ."));
  using json = nlohmann::json;
  arli::StreamState state;
  auto delta = [&](json value, json finish = nullptr) {
    const std::string wire = "data: " + json{{"choices", json::array({json{{"delta", value}, {"finish_reason", finish}}})}}.dump() + "\n\n";
    // Feed in small network fragments, including across JSON and UTF-8 boundaries.
    for (size_t i = 0; i < wire.size(); i += 3) arli::feed_stream_bytes(state, wire.data() + i, std::min(size_t(3), wire.size() - i));
  };
  delta({{"tool_calls", json::array({json{{"index", 0}, {"id", "call_a"}, {"function", {{"name", "edit_"}, {"arguments", "{\"path\":"}}}}})}});
  delta({{"tool_calls", json::array({json{{"index", 0}, {"function", {{"name", "file"}, {"arguments", "\"a.txt\",\"new_text\":\"да\"}"}}}}})}}, "tool_calls");
  const std::string usage = "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":123,\"completion_tokens\":17}}\n\n";
  arli::feed_stream_bytes(state, usage.data(), usage.size());
  auto reply = arli::reply_from_stream_state(state);
  CHECK(reply.prompt_tokens == 123 && reply.completion_tokens == 17);
  CHECK(reply.ok);
  CHECK(reply.message["tool_calls"][0]["function"]["name"] == "edit_file");
  CHECK(json::parse(reply.message["tool_calls"][0]["function"]["arguments"].get<std::string>())["new_text"] == "да");
  auto broken = state;
  broken.finish_reason.clear();
  CHECK(!arli::reply_from_stream_state(broken).ok);
  broken.finish_reason = "length";
  CHECK(!arli::reply_from_stream_state(broken).ok);
  CHECK(!arli::parse_non_stream_body(R"({"choices":[{"message":{"role":"assistant","tool_calls":[{"function":{"name":5}}]}}]})", nullptr).ok);
  CHECK(!arli::parse_non_stream_body(R"({"choices":[{"message":{"role":"assistant","content":"partial"},"finish_reason":"length"}]})", nullptr).ok);
  arli::LlmConfig cfg;
  cfg.model = "test";
  cfg.profile = "generic";
  const json tools = json::array({json{{"type", "function"}}});
  CHECK(arli::llm_internal::build_chat_body(cfg, json::array(), tools, "required", false, "generic")["tool_choice"] == "required");
  const auto qwen = arli::llm_internal::build_chat_body(cfg, json::array(), tools, "required", false, "qwen3");
  CHECK(qwen["tool_choice"] == "required" && qwen["parallel_tool_calls"] == false);
  CHECK(qwen["temperature"] == 1.0 && qwen["top_p"] == 0.95 && qwen["top_k"] == 20);
  CHECK(qwen["reasoning_effort"] == "xhigh" && qwen["reasoning_budget_tokens"] == 2048);
  CHECK(arli::llm_internal::build_chat_body(cfg, json::array(), tools, "required", false, "gemma4")["tool_choice"] == "auto");
  const auto gemma = arli::llm_internal::build_chat_body(cfg, json::array(), tools, "auto", false, "gemma4");
  CHECK(gemma["temperature"] == 1.0);
  CHECK(gemma["top_p"] == 0.95 && gemma["top_k"] == 64);
  CHECK(gemma["reasoning_budget_tokens"] == 2048 && gemma["max_tokens"] == 8192);
  CHECK(!gemma.contains("thinking_budget_tokens"));
  CHECK(gemma["response_format"]["type"] == "json_schema");
  CHECK(arli::llm_internal::build_chat_body(cfg, json::array(), tools, "auto", false, "generic")["temperature"] == 0.2);
  cfg.temperature = 0.0;
  for (const auto* profile : {"generic", "gemma4", "qwen3"})
    CHECK(arli::llm_internal::build_chat_body(cfg, json::array(), tools, "auto", true, profile)["temperature"] == 0.0);
  cfg.temperature = 2.0;
  CHECK(arli::llm_internal::build_chat_body(cfg, json::array(), tools, "auto", false, "gemma4")["temperature"] == 2.0);
  for (double invalid : {-0.1, 2.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    cfg.temperature = invalid;
    bool rejected = false;
    try { arli::llm_internal::build_chat_body(cfg, json::array(), tools, "auto", false, "gemma4"); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
  }
  cfg.temperature = -1.0;
  auto qwen_tools = json::parse(arli::tools_openai_schema_json());
  arli::filter_tool_schemas(qwen_tools, "qwen3");
  bool legacy_edit=false,strict_edit=false,batch_edit=false;
  for(const auto& tool:qwen_tools){const auto name=tool["function"]["name"].get<std::string>();
    legacy_edit|=name=="edit_file";strict_edit|=name=="replace_text";batch_edit|=name=="batch_replace";}
  CHECK(!legacy_edit&&strict_edit&&batch_edit);
  const json catalog = json::array({json{{"type", "function"}, {"function", {
      {"name", "edit_file"}, {"parameters", {{"type", "object"}, {"properties", {
        {"path", {{"type", "string"}}}, {"search", {{"type", "string"}}}, {"text", {{"type", "string"}}}}},
        {"required", {"path", "search", "text"}}}}}}}});
  arli::LlmReply structured;
  structured.ok = true; structured.message = {{"role", "assistant"}};
  structured.content = R"({"action":"tool","name":"edit_file","params":{"path":"a.js","search":"old","text":"new"}})";
  arli::decode_structured_tool_reply(structured, catalog);
  CHECK(structured.ok && structured.content.empty() && structured.message["tool_calls"][0]["function"]["name"] == "edit_file");
  structured.message.erase("tool_calls"); structured.content = R"({"action":"tool","name":"edit_file","params":{"text":"new"}})";
  arli::decode_structured_tool_reply(structured, catalog); CHECK(!structured.ok);
  structured.ok = true; structured.content = R"({"action":"tool","name":"unknown","params":{}})";
  arli::decode_structured_tool_reply(structured, catalog); CHECK(!structured.ok);
  return 0;
}
