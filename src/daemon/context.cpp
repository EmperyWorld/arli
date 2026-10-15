#include "context.hpp"

#include "context_summary.hpp"

#include <string>

namespace arli {
namespace context {
namespace {

int chars_of(const nlohmann::json& v) {
  if (v.is_string()) {
    return static_cast<int>(v.get_ref<const std::string&>().size());
  }
  if (v.is_array() || v.is_object()) {
    int n = 0;
    for (const auto& child : v) {
      n += chars_of(child);
    }
    return n;
  }
  return static_cast<int>(v.dump().size());
}

std::string role_of(const nlohmann::json& m) {
  return m.is_object() ? m.value("role", "") : std::string();
}

int system_end(const nlohmann::json& messages) {
  int i = 0;
  while (i < static_cast<int>(messages.size()) && role_of(messages[i]) == "system") {
    ++i;
  }
  return i;
}

// A tail must not start on a tool result: that would orphan it from its assistant call.
int align_tail(const nlohmann::json& messages, int index) {
  const int head = system_end(messages);
  while (index > head && role_of(messages[index]) == "tool") {
    --index;
  }
  return index;
}

void shrink_tools(nlohmann::json& messages, std::size_t max_chars) {
  for (auto& m : messages) {
    if (role_of(m) != "tool" || !m.contains("content") || !m["content"].is_string()) {
      continue;
    }
    auto& text = m["content"].get_ref<std::string&>();
    if (text.size() > max_chars) {
      text = tool_output_for_model(text, max_chars);
    }
  }
}

}  // namespace

int estimate_tokens(const nlohmann::json& messages) {
  const int chars = chars_of(messages);
  return chars <= 0 ? 0 : (chars + 3) / 4;
}

bool compact(nlohmann::json& messages, int window) {
  if (!messages.is_array() || messages.size() < 4 || window < 1024) {
    return false;
  }
  const int limit = window * 4 / 5;
  if (estimate_tokens(messages) <= limit) {
    return false;
  }

  const int head = system_end(messages);
  int tail = static_cast<int>(messages.size()) - 1;
  int tail_chars = 0;
  const int tail_budget = window * 2;  // ~half the window, in characters
  while (tail > head) {
    tail_chars += chars_of(messages[tail]);
    if (tail_chars > tail_budget) {
      break;
    }
    --tail;
  }
  tail = align_tail(messages, tail);
  if (tail <= head) {
    tail = align_tail(messages, head + 1);
  }
  if (tail <= head || tail >= static_cast<int>(messages.size())) {
    shrink_tools(messages, 1500);
    return true;
  }

  const std::string note = build_fold_summary(messages, head, tail);

  nlohmann::json next = nlohmann::json::array();
  for (int i = 0; i < head; ++i) {
    next.push_back(messages[i]);
  }
  next.push_back({{"role", "user"}, {"content", note}});
  for (int i = tail; i < static_cast<int>(messages.size()); ++i) {
    next.push_back(messages[i]);
  }
  shrink_tools(next, 2000);
  messages = std::move(next);
  return true;
}

void fit_budget(nlohmann::json& messages, int window) {
  if (window < 1024) {
    return;
  }
  const int limit = window * 4 / 5;
  for (int pass = 0; pass < 10 && estimate_tokens(messages) > limit; ++pass) {
    const int before = estimate_tokens(messages);
    if (!compact(messages, window)) {
      shrink_tools(messages, pass > 3 ? 800 : 1500);
    }
    if (estimate_tokens(messages) >= before && !compact(messages, window)) {
      break;
    }
  }
}

}  // namespace context
}  // namespace arli
