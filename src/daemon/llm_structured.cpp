#include "llm_structured.hpp"

namespace arli {
using json = nlohmann::json;
json structured_tool_schema(const json& tools) {
  json variants = json::array({json{{"type", "object"}, {"properties", {
      {"action", {{"const", "final"}}}, {"answer", {{"type", "string"}}}}},
      {"required", {"action", "answer"}}, {"additionalProperties", false}}});
  for (const auto& tool : tools) {
    if (!tool.contains("function") || !tool["function"].contains("parameters")) continue;
    const auto& fn = tool["function"];
    json params = fn["parameters"];
    params["additionalProperties"] = false;
    variants.push_back({{"type", "object"}, {"properties", {
        {"action", {{"const", "tool"}}}, {"name", {{"const", fn["name"]}}}, {"params", params}}},
        {"required", {"action", "name", "params"}}, {"additionalProperties", false}});
  }
  return {{"oneOf", variants}};
}
void decode_structured_tool_reply(LlmReply& reply, const json& tools) {
  if (!reply.ok || (reply.message.contains("tool_calls") && !reply.message["tool_calls"].empty())) return;
  const auto data = json::parse(reply.content, nullptr, false);
  // Compatibility with servers/mocks that ignore response_format and return ordinary messages.
  if (!data.is_object() || !data.contains("action")) return;
  auto fail = [&] { reply.ok = false; reply.error = "LLM вернула неверный JSON выбора инструмента; ничего не исполнено"; };
  if (data["action"] == "final") {
    if (!data.contains("answer") || !data["answer"].is_string()) { fail(); return; }
    reply.content = data["answer"].get<std::string>();
    reply.message["content"] = reply.content;
    return;
  }
  if (data["action"] != "tool" || !data.contains("name") || !data["name"].is_string() ||
      !data.contains("params") || !data["params"].is_object()) { fail(); return; }
  const json* fn = nullptr;
  for (const auto& tool : tools)
    if (tool.contains("function") && tool["function"].value("name", "") == data["name"].get<std::string>())
      fn = &tool["function"];
  if (!fn) { fail(); return; }
  const auto& args = data["params"];
  const auto& schema = (*fn)["parameters"];
  for (const auto& key : schema.value("required", json::array()))
    if (!args.contains(key.get<std::string>())) { fail(); return; }
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (!schema["properties"].contains(it.key())) { fail(); return; }
    const auto type = schema["properties"][it.key()].value("type", "");
    const auto& v = it.value();
    if ((type == "string" && !v.is_string()) || (type == "integer" && !v.is_number_integer()) ||
        (type == "boolean" && !v.is_boolean()) || (type == "array" && !v.is_array()) ||
        (type == "object" && !v.is_object()) || (type == "number" && !v.is_number())) { fail(); return; }
  }
  reply.content.clear();
  reply.message["content"] = nullptr;
  static std::atomic<unsigned long> next{0};
  reply.message["tool_calls"] = json::array({json{{"id", "structured_" + std::to_string(++next)}, {"type", "function"},
      {"function", {{"name", data["name"]}, {"arguments", args.dump()}}}}});
}
}
