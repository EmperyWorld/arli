#include "llm_internal.hpp"
#include "llm_sse.hpp"
#include "llm_tool_format.hpp"
#include "llm_structured.hpp"

#include "llm.hpp"

#include <curl/curl.h>

#include <cctype>
#include <chrono>
#include <thread>

namespace arli {
namespace {

struct CurlStreamCtx {
  StreamState* st;
  std::string* fallback_body;
};

size_t stream_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* ctx = static_cast<CurlStreamCtx*>(userdata);
  if (ctx->st) {
    feed_stream_bytes(*ctx->st, ptr, n);
    return n;
  }
  if (ctx->fallback_body) {
    ctx->fallback_body->append(ptr, n);
  }
  return n;
}

int progress_cb(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* cancel = static_cast<std::atomic<bool>*>(clientp);
  return (cancel && cancel->load()) ? 1 : 0;
}

bool retryable_error(const LlmReply& r) {
  if (r.ok || r.cancelled) {
    return false;
  }
  if (r.error.rfind("CONTEXT_LIMIT", 0) == 0) return false;
  if (r.error.rfind("HTTP 4", 0) == 0) {
    return false;
  }
  if (r.error.rfind("bad json", 0) == 0 || r.error.rfind("parse choices", 0) == 0) {
    return false;
  }
  return true;
}

LlmReply chat_completions_once(const LlmConfig& cfg, const nlohmann::json& messages, const nlohmann::json& tools,
                               const std::string& tool_choice, std::atomic<bool>* cancel, LlmStreamSink* stream) {
  LlmReply reply;
  const bool want_stream = stream != nullptr;
  const nlohmann::json body =
      llm_internal::build_chat_body(cfg, messages, tools, tool_choice, want_stream, cfg.profile);
  if (cfg.context_tokens > 0 &&
      static_cast<std::int64_t>(llm_internal::request_prompt_tokens(body, cfg.prompt_token_scale)) + body.value("max_tokens", 0) > cfg.context_tokens) {
    reply.error = "CONTEXT_LIMIT: инструкции, история и схемы инструментов не помещаются вместе с резервом ответа; увеличьте окно контекста или сократите задание.";
    return reply;
  }
  const bool structured = body.contains("response_format");
  LlmStreamSink buffered;
  if (stream) buffered.on_thought = stream->on_thought;
  LlmStreamSink* wire_sink = structured ? &buffered : stream;
  auto finish = [&](LlmReply value) {
    if (structured) {
      decode_structured_tool_reply(value, tools);
      // The turn validates completion before publishing the final content to the UI.
    }
    return value;
  };
  const std::string payload = body.dump();
  const std::string url = llm_internal::join_url(cfg.base_url, "/chat/completions");

  CURL* curl = curl_easy_init();
  if (!curl) {
    reply.error = "curl init failed";
    return reply;
  }

  StreamState st;
  st.sink = wire_sink;
  st.cancel = cancel;
  std::string fallback_body;
  CurlStreamCtx ctx{want_stream ? &st : nullptr, want_stream ? nullptr : &fallback_body};

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  if (want_stream) {
    headers = curl_slist_append(headers, "Accept: text/event-stream");
  }
  if (!cfg.api_key.empty()) {
    const std::string auth = "Authorization: Bearer " + cfg.api_key;
    headers = curl_slist_append(headers, auth.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  if (cancel) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc == CURLE_ABORTED_BY_CALLBACK || (cancel && cancel->load())) {
    reply.cancelled = true;
    reply.error = "отменено";
    return reply;
  }
  if (rc != CURLE_OK) {
    reply.error = std::string("curl: ") + curl_easy_strerror(rc);
    return reply;
  }
  if (http < 200 || http >= 300) {
    return http_error_reply(http, want_stream ? st.raw : fallback_body);
  }

  if (want_stream) {
    if (st.content.empty() && st.reasoning.empty() && st.tool_calls.empty() && !st.raw.empty()) {
      std::string trimmed = st.raw;
      while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
        trimmed.erase(trimmed.begin());
      }
      if (!trimmed.empty() && trimmed.front() == '{') {
        return finish(parse_non_stream_body(st.raw, wire_sink));
      }
    }
    return finish(reply_from_stream_state(st));
  }

  return finish(parse_non_stream_body(fallback_body, nullptr));
}

}  // namespace

LlmReply chat_completions(const LlmConfig& cfg, const nlohmann::json& messages, const nlohmann::json& tools,
                          const std::string& tool_choice, std::atomic<bool>* cancel, LlmStreamSink* stream) {
  LlmReply reply;
  if (cancel && cancel->load()) {
    reply.cancelled = true;
    reply.error = "отменено";
    return reply;
  }
  if (cfg.base_url.empty() || cfg.model.empty()) {
    reply.error = "llm_base_url and llm_model required";
    return reply;
  }

  constexpr int kAttempts = 3;
  LlmReply last;
  nlohmann::json retry_messages = messages;
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    if (cancel && cancel->load()) {
      last.cancelled = true;
      last.error = "отменено";
      return last;
    }
    last = chat_completions_once(cfg, retry_messages, tools, tool_choice, cancel, stream);
    if (cfg.profile == "gemma4" && tool_choice != "none" && tools.is_array() && !tools.empty() &&
        unparsed_tool_reply(last)) {
      retry_messages.push_back(last.message);
      retry_messages.push_back({{"role", "user"}, {"content", kToolFormatRepair}});
      last.ok = false;
      last.error = "LLM вывела вызов инструмента текстом; вызов не исполнен. Не удалось исправить формат ответа.";
    }
    if (!retryable_error(last) || attempt + 1 >= kAttempts) {
      return last;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
  }
  return last;
}

}  // namespace arli
