#include "log.hpp"
#include "redact.hpp"

#include "paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

namespace arli::log {
namespace {

constexpr std::uintmax_t kMaxBytes = 5 * 1024 * 1024;

std::atomic<int> g_level{-1};  // -1 → read ARLI_LOG lazily
std::mutex g_mu;

Level env_level() {
  if (const char* e = std::getenv("ARLI_LOG"); e && *e) {
    return parse_level(e);
  }
  return Level::Info;
}

const char* name(Level l) {
  switch (l) {
    case Level::Debug:
      return "DEBUG";
    case Level::Info:
      return "INFO ";
    case Level::Warn:
      return "WARN ";
    case Level::Error:
      return "ERROR";
  }
  return "?";
}

std::string now_iso() {
  using clock = std::chrono::system_clock;
  const auto t = clock::to_time_t(clock::now());
  std::tm utc{};
  if (::gmtime_r(&t, &utc) == nullptr) {
    return "1970-01-01T00:00:00Z";
  }
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buf;
}

void rotate_if_needed(const std::filesystem::path& file) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size < kMaxBytes) {
    return;
  }
  std::filesystem::rename(file, file.string() + ".1", ec);
}

}  // namespace

Level parse_level(std::string_view s) {
  if (s == "debug") {
    return Level::Debug;
  }
  if (s == "warn" || s == "warning") {
    return Level::Warn;
  }
  if (s == "error") {
    return Level::Error;
  }
  return Level::Info;
}

Level level() {
  int l = g_level.load();
  if (l < 0) {
    l = static_cast<int>(env_level());
    g_level.store(l);
  }
  return static_cast<Level>(l);
}

void set_level(Level l) {
  g_level.store(static_cast<int>(l));
}

void write(Level l, std::string_view component, std::string_view line) {
  if (static_cast<int>(l) < static_cast<int>(level())) {
    return;
  }
  const std::string out = now_iso() + " " + name(l) + " " + redact::text(component) + " " + redact::text(line) + "\n";
  std::lock_guard lock(g_mu);
  std::fputs(out.c_str(), stderr);
  try {
    const auto file = state_root() / "arli.log";
    std::filesystem::create_directories(file.parent_path());
    rotate_if_needed(file);
    std::ofstream f(file, std::ios::app);
    if (f) {
      f << out;
    }
  } catch (...) {
  }
}

std::string kv(std::string_view key, long long value) {
  return std::string(key) + "=" + std::to_string(value);
}

std::string kv(std::string_view key, std::string_view value) {
  std::string v(value);
  for (auto& c : v) {
    if (c == '\n' || c == '\r' || c == '\t') {
      c = ' ';
    }
  }
  if (v.find(' ') != std::string::npos || v.empty()) {
    return std::string(key) + "=\"" + v + "\"";
  }
  return std::string(key) + "=" + v;
}

}  // namespace arli::log
