#pragma once
#include "llm.hpp"
#include <cctype>

namespace arli {
// Detect a trailing attempted invocation, not quoted examples in ordinary prose.
inline bool unparsed_tool_tail(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
  return (text.find("<call:") != std::string::npos && text.ends_with("/>")) ||
         (text.find("<|tool_call>call:") != std::string::npos && text.ends_with("<tool_call|>"));
}
inline bool unparsed_tool_reply(const LlmReply& reply) {
  return reply.ok && unparsed_tool_tail(reply.content) &&
      (!reply.message.contains("tool_calls") || reply.message["tool_calls"].empty());
}
inline constexpr const char* kToolFormatRepair =
    "Предыдущий ответ не вызвал инструмент: тег <call:…/> оказался обычным текстом. "
    "Ни один из этих вызовов не исполнен. Не жди их результатов. "
    "Для вызова используй нативный формат Gemma: "
    "<|tool_call>call:list_dir{path:<|\"|>.<|\"|>}<tool_call|>. "
    "Имя и аргументы возьми из доступных tools; не помещай вызов в Markdown или пример кода. "
    "Продолжи текущую задачу настоящим вызовом инструмента.";
}
