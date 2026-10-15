#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace arli {
namespace context {

// Last index at which [0, cut) is valid UTF-8. A cut inside a multibyte
// character makes nlohmann throw and kills the turn.
inline std::size_t utf8_floor(const std::string& s, std::size_t max) {
  if (max >= s.size()) {
    return s.size();
  }
  std::size_t i = max;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto lead = static_cast<unsigned char>(s[i]);
  std::size_t need = 1;
  if ((lead & 0xE0) == 0xC0) {
    need = 2;
  } else if ((lead & 0xF0) == 0xE0) {
    need = 3;
  } else if ((lead & 0xF8) == 0xF0) {
    need = 4;
  } else if ((lead & 0x80) != 0) {
    return i;
  }
  return i + need > max ? i : max;
}

inline std::string clip_text(std::string s, std::size_t max) {
  const std::size_t cut = utf8_floor(s, max);
  if (cut >= s.size()) {
    return s;
  }
  s.resize(cut);
  s += "...";
  return s;
}

// Head + tail clip for tool stdout in the LLM transcript (full output stays in steps).
inline std::string clip_text_head_tail(std::string s, std::size_t max) {
  static constexpr const char kMid[] = "\n… [середина обрезана, полный вывод в шагах] …\n";
  const std::size_t mid_len = sizeof(kMid) - 1;
  if (s.size() <= max) {
    return s;
  }
  if (max <= mid_len + 32) {
    return clip_text(std::move(s), max);
  }
  const std::size_t budget = max - mid_len;
  const std::size_t head_sz = budget * 3 / 5;
  const std::size_t tail_sz = budget - head_sz;
  std::string head = s.substr(0, utf8_floor(s, head_sz));
  std::size_t tail_start = s.size() > tail_sz ? s.size() - tail_sz : 0;
  while (tail_start < s.size() && (static_cast<unsigned char>(s[tail_start]) & 0xC0) == 0x80) {
    ++tail_start;
  }
  return head + std::string(kMid) + s.substr(tail_start);
}

inline std::string tool_output_for_model(const std::string& full, std::size_t max_chars = 6000) {
  return clip_text_head_tail(full, max_chars);
}

// Source reads may use roughly one quarter of the configured token window.
inline std::size_t source_output_limit(int window) {
  return static_cast<std::size_t>(window < 6000 ? 6000 : window > 48000 ? 48000 : window);
}

// Rough token count: about 4 characters per token, across every string in the messages.
int estimate_tokens(const nlohmann::json& messages);

// When the transcript is past 80% of `window`, fold older turns into one note and
// shorten huge tool outputs. The system message and the live tail stay intact.
// Returns true when `messages` was rewritten.
bool compact(nlohmann::json& messages, int window);

// Repeat compact / tool shrink until under ~80% of `window` or no progress.
void fit_budget(nlohmann::json& messages, int window);

}  // namespace context
}  // namespace arli
