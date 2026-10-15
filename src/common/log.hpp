#pragma once

#include <string>
#include <string_view>

// Structured one-line log shared by arli-daemon and arli-agent:
//   2026-10-08T11:30:05Z INFO  orch  task=12 tool=read_file ok=1 dur_ms=3 msg
// Sinks: stderr + state_root()/arli.log (rotated at kMaxBytes → arli.log.1).
// Level via ARLI_LOG=debug|info|warn|error (default info). Never log secrets.
namespace arli::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

Level level();
void set_level(Level l);
Level parse_level(std::string_view s);

void write(Level l, std::string_view component, std::string_view line);

inline void debug(std::string_view component, std::string_view line) { write(Level::Debug, component, line); }
inline void info(std::string_view component, std::string_view line) { write(Level::Info, component, line); }
inline void warn(std::string_view component, std::string_view line) { write(Level::Warn, component, line); }
inline void error(std::string_view component, std::string_view line) { write(Level::Error, component, line); }

// Helpers to build `key=value` fragments.
std::string kv(std::string_view key, long long value);
std::string kv(std::string_view key, std::string_view value);  // quotes when the value has spaces

}  // namespace arli::log
