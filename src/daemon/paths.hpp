#pragma once

#include "version.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace arli {

inline constexpr const char* kListenEndpoint = "127.0.0.1:4792";

inline std::filesystem::path state_root() {
  if (const char* forced = std::getenv("ARLI_STATE_DIR"); forced && *forced) {
    return std::filesystem::path(forced);
  }
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "arli";
  }
  const char* home = std::getenv("HOME");
  if (!home || !*home) {
    home = "/tmp";
  }
  return std::filesystem::path(home) / ".local" / "share" / "arli";
}

inline std::filesystem::path db_path() {
  return state_root() / "arli.db";
}

inline bool listen_endpoint_allowed(const std::string& endpoint) {
  return endpoint.rfind("127.0.0.1:", 0) == 0 || endpoint.rfind("localhost:", 0) == 0;
}

}  // namespace arli
