#include "llm_sse.hpp"

#include <cctype>
#include <limits>

namespace arli {
namespace {

int usage_token(const nlohmann::json& value, const char* key) {
  if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer()) return 0;
  const auto number = value[key].get<std::int64_t>();
  return number >= 0 && number <= std::numeric_limits<int>::max() ? static_cast<int>(number) : 0;
}

bool emit_delta(LlmStreamSink* sink, const char* kind, std::string_view delta) {
  if (!sink || delta.empty()) {
    return true;
  }
  if (kind[0] == 'c' && sink->on_token) {
    return sink->on_token(std::string(delta));
  }
  if (kind[0] == 'r' && sink->on_thought) {
    return sink->on_thought(std::string(delta));
  }
  return true;
}

void merge_tool_delta(nlohmann::json& acc, const nlohmann::json& tc) {
  if (tc.contains("id") && tc["id"].is_string()) {
    acc["id"] = tc["id"];
  }
  if (tc.contains("type") && tc["type"].is_string()) {
    acc["type"] = tc["type"];
  }
  if (!tc.contains("function") || !tc["function"].is_object()) {
    return;
  }
  if (!acc.contains("function")) {
    acc["function"] = nlohmann::json{{"name", ""}, {"arguments", ""}};
  }
  const auto& fn = tc["function"];
  if (fn.contains("name") && fn["name"].is_string()) {
    acc["function"]["name"] = acc["function"]["name"].get<std::string>() + fn["name"].get<std::string>();
  }
  if (fn.contains("arguments") && fn["arguments"].is_string()) {
    std::string prev = acc["function"]["arguments"].get<std::string>();
    prev += fn["arguments"].get<std::string>();
    acc["function"]["arguments"] = prev;
  }
}

void handle_sse_json(StreamState& st, const nlohmann::json& j) {
  if (j.contains("usage")) {
    st.prompt_tokens = usage_token(j["usage"], "prompt_tokens");
    st.completion_tokens = usage_token(j["usage"], "completion_tokens");
  }
  if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
    return;
  }
  const auto& choice = j["choices"][0];
  if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
    st.finish_reason = choice["finish_reason"].get<std::string>();
  nlohmann::json delta = choice.contains("delta") ? choice["delta"] : nlohmann::json::object();
  if (delta.is_null()) {
    delta = nlohmann::json::object();
  }
  if (delta.contains("content") && delta["content"].is_string()) {
    const std::string piece = delta["content"].get<std::string>();
    st.content += piece;
    if (!emit_delta(st.sink, "content", piece) && st.cancel) {
      st.cancel->store(true);
    }
  }
  if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
    const std::string piece = delta["reasoning_content"].get<std::string>();
    st.reasoning += piece;
    if (!emit_delta(st.sink, "reasoning", piece) && st.cancel) {
      st.cancel->store(true);
    }
  }
  if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
    for (const auto& tc : delta["tool_calls"]) {
      const int idx = tc.value("index", 0);
      if (idx < 0 || idx > 128) { st.error = "invalid tool index"; return; }
      merge_tool_delta(st.tool_calls[idx], tc);
    }
  }
}

void consume_sse_line(StreamState& st, std::string line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  if (line.rfind("data:", 0) != 0) {
    return;
  }
  std::string payload = line.substr(5);
  while (!payload.empty() && payload.front() == ' ') {
    payload.erase(payload.begin());
  }
  if (payload == "[DONE]") {
    st.saw_done = true;
    return;
  }
  try {
    handle_sse_json(st, nlohmann::json::parse(payload));
  } catch (const std::exception&) {
    st.error = "bad SSE chunk";
  }
}

void feed_sse(StreamState& st, const char* data, size_t len) {
  st.line_buf.append(data, len);
  for (;;) {
    const auto pos = st.line_buf.find('\n');
    if (pos == std::string::npos) {
      break;
    }
    std::string line = st.line_buf.substr(0, pos);
    st.line_buf.erase(0, pos + 1);
    consume_sse_line(st, std::move(line));
  }
}

nlohmann::json assistant_message_from_state(const StreamState& st) {
  nlohmann::json msg = {{"role", "assistant"}};
  if (!st.content.empty()) {
    msg["content"] = st.content;
  } else {
    msg["content"] = nullptr;
  }
  if (!st.reasoning.empty()) {
    msg["reasoning_content"] = st.reasoning;
  }
  if (!st.tool_calls.empty()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [idx, tc] : st.tool_calls) {
      (void)idx;
      nlohmann::json call = tc;
      if (!call.contains("type")) {
        call["type"] = "function";
      }
      arr.push_back(std::move(call));
    }
    msg["tool_calls"] = std::move(arr);
  }
  return msg;
}

bool validate_reply(LlmReply& reply, const std::string& finish) {
  if (finish == "length") { reply.error = "ответ LLM обрезан лимитом токенов; инструменты не исполнены"; return false; }
  if (!reply.message.is_object()) { reply.error = "invalid assistant message"; return false; }
  if (!reply.message.contains("tool_calls")) return true;
  auto& calls = reply.message["tool_calls"];
  if (!calls.is_array()) { reply.error = "tool_calls must be an array"; return false; }
  int index = 0;
  for (auto& call : calls) {
    if (!call.is_object() || !call.contains("function") || !call["function"].is_object() ||
        !call["function"].contains("name") || !call["function"]["name"].is_string() ||
        call["function"]["name"].get<std::string>().empty()) {
      reply.error = "invalid tool call function"; return false;
    }
    if (!call.contains("id") || !call["id"].is_string() || call["id"].get<std::string>().empty())
      call["id"] = "call_" + std::to_string(index);
    ++index;
    auto& fn = call["function"];
    if (!fn.contains("arguments")) fn["arguments"] = "{}";
    if (!fn["arguments"].is_string() && !fn["arguments"].is_object()) {
      reply.error = "invalid tool call arguments"; return false;
    }
  }
  return true;
}

}  // namespace

void feed_stream_bytes(StreamState& st, const char* data, size_t len) {
  st.raw.append(data, len);
  feed_sse(st, data, len);
}

LlmReply parse_non_stream_body(const std::string& response, LlmStreamSink* sink) {
  LlmReply reply;
  try {
    reply.raw = nlohmann::json::parse(response);
    if (reply.raw.contains("usage")) {
      reply.prompt_tokens = usage_token(reply.raw["usage"], "prompt_tokens");
      reply.completion_tokens = usage_token(reply.raw["usage"], "completion_tokens");
    }
  } catch (const std::exception& e) {
    reply.error = std::string("bad json: ") + e.what();
    return reply;
  }
  try {
    reply.message = reply.raw.at("choices").at(0).at("message");
    if (reply.message.contains("content") && reply.message["content"].is_string()) {
      reply.content = reply.message["content"].get<std::string>();
      emit_delta(sink, "content", reply.content);
    }
    if (reply.message.contains("reasoning_content") && reply.message["reasoning_content"].is_string()) {
      emit_delta(sink, "reasoning", reply.message["reasoning_content"].get<std::string>());
    }
    const auto& choice = reply.raw.at("choices").at(0);
    const std::string finish = choice.contains("finish_reason") && choice["finish_reason"].is_string()
                                 ? choice["finish_reason"].get<std::string>() : std::string();
    reply.ok = validate_reply(reply, finish);
  } catch (const std::exception& e) {
    reply.error = std::string("parse choices: ") + e.what();
  }
  return reply;
}

LlmReply reply_from_stream_state(const StreamState& st) {
  LlmReply reply;
  StreamState complete = st;
  if (!complete.line_buf.empty()) consume_sse_line(complete, complete.line_buf);
  if (!complete.error.empty()) { reply.error = complete.error; return reply; }
  if (!complete.saw_done && complete.finish_reason.empty()) {
    reply.error = "поток LLM оборвался до завершения; инструменты не исполнены";
    return reply;
  }
  const nlohmann::json message = assistant_message_from_state(complete);
  reply.raw = {{"object", "chat.completion"},
               {"choices", nlohmann::json::array({{{"index", 0}, {"message", message}}})}};
  reply.prompt_tokens = complete.prompt_tokens;
  reply.completion_tokens = complete.completion_tokens;
  reply.message = message;
  if (reply.message.contains("content") && reply.message["content"].is_string()) {
    reply.content = reply.message["content"].get<std::string>();
  }
  reply.ok = validate_reply(reply, complete.finish_reason);
  return reply;
}

LlmReply http_error_reply(long http, const std::string& response) {
  LlmReply reply;
  const std::string detail = response.substr(0, 500);
  if (detail.find("Failed to initialize samplers") != std::string::npos ||
      detail.find("grammar") != std::string::npos) {
    reply.error = "HTTP " + std::to_string(http) +
                  ": llama.cpp/Gemma не смог собрать grammar для tools (sampler). "
                  "Обычно это tool_choice=required или битое сообщение в истории. "
                  "Повтори ход или создай новый диалог. detail=" +
                  detail.substr(0, 180);
  } else {
    reply.error = "HTTP " + std::to_string(http) + ": " + detail.substr(0, 400);
  }
  return reply;
}

}  // namespace arli
