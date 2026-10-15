#pragma once

#include "llm.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <string>

namespace arli {

struct StreamState {
  std::string line_buf;
  std::string raw;
  std::string content;
  std::string reasoning;
  std::string finish_reason;
  std::string error;
  bool saw_done = false;
  int prompt_tokens = 0;
  int completion_tokens = 0;
  std::map<int, nlohmann::json> tool_calls;
  LlmStreamSink* sink = nullptr;
  std::atomic<bool>* cancel = nullptr;
};

void feed_stream_bytes(StreamState& st, const char* data, size_t len);

LlmReply parse_non_stream_body(const std::string& response, LlmStreamSink* sink);

LlmReply reply_from_stream_state(const StreamState& st);

LlmReply http_error_reply(long http, const std::string& response);

}  // namespace arli
