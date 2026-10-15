#include "secrets.hpp"
#include "redact.hpp"

#include "paths.hpp"

#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace arli::secrets {
namespace {

std::string urlsafe_b64(const unsigned char* data, size_t n) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  size_t i = 0;
  while (i + 2 < n) {
    const unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
    out.push_back(tbl[v & 63]);
    i += 3;
  }
  if (i + 1 == n) {
    const unsigned v = data[i] << 16;
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
  } else if (i + 2 == n) {
    const unsigned v = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(tbl[(v >> 18) & 63]);
    out.push_back(tbl[(v >> 12) & 63]);
    out.push_back(tbl[(v >> 6) & 63]);
  }
  return out;
}

std::string generate() {
  unsigned char buf[32];
  size_t got = 0;
  while (got < sizeof(buf)) {
    const ssize_t n = getrandom(buf + got, sizeof(buf) - got, 0);
    if (n <= 0) {
      break;
    }
    got += static_cast<size_t>(n);
  }
  if (got < sizeof(buf)) {
    // Fallback: /dev/urandom.
    std::ifstream ur("/dev/urandom", std::ios::binary);
    ur.read(reinterpret_cast<char*>(buf + got), static_cast<std::streamsize>(sizeof(buf) - got));
  }
  return urlsafe_b64(buf, sizeof(buf));
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
  return s;
}

}  // namespace

std::filesystem::path ui_token_path() {
  return state_root() / "ui_token";
}

std::filesystem::path internal_token_path() {
  return state_root() / "internal_token";
}

std::string load(const std::filesystem::path& file) {
  std::ifstream in(file);
  if (!in) {
    return {};
  }
  std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  s=trim(s);redact::remember(s);return s;
}

std::string load_or_create(const std::filesystem::path& file) {
  std::string existing = load(file);
  if (!existing.empty()) {
    chmod(file.c_str(), 0600);
    return existing;
  }
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const std::string token = generate();
  redact::remember(token);
  const auto tmp = file.string() + ".tmp";
  const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) {
    return {};
  }
  const std::string body = token + "\n";
  size_t off = 0;
  while (off < body.size()) {
    const ssize_t n = write(fd, body.data() + off, body.size() - off);
    if (n <= 0) {
      close(fd);
      unlink(tmp.c_str());
      return {};
    }
    off += static_cast<size_t>(n);
  }
  fsync(fd);
  close(fd);
  if (rename(tmp.c_str(), file.c_str()) != 0) {
    unlink(tmp.c_str());
    return {};
  }
  return token;
}

std::string ui_token() {
  if (const char* env = std::getenv("ARLI_UI_TOKEN"); env && *env) {
    redact::remember(env);return env;
  }
  return load(ui_token_path());
}

std::string internal_token() {
  if (const char* env = std::getenv("ARLI_INTERNAL_TOKEN"); env && *env) {
    redact::remember(env);return env;
  }
  return load(internal_token_path());
}

bool constant_time_equal(std::string_view a, std::string_view b) {
  // Compare against the longer length to avoid leaking length via early exit.
  const size_t n = a.size() > b.size() ? a.size() : b.size();
  unsigned diff = a.size() ^ b.size();
  for (size_t i = 0; i < n; ++i) {
    const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
    const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    diff |= ca ^ cb;
  }
  return diff == 0;
}

}  // namespace arli::secrets
