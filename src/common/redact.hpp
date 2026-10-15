#pragma once
#include <string>
#include <filesystem>
#include <string_view>
#include <google/protobuf/message.h>
namespace arli::redact {
// Process-local known credentials; rotations remain protected until process exit.
void remember(std::string_view secret);
// Opt-in durable SHA-256 fingerprints, not raw credentials; private file required.
void enable_persistence(const std::filesystem::path& path);
void ensure_available(); // Throw before executing a turn/event if durable protection is unavailable.
std::string text(std::string_view input);
void message(google::protobuf::Message& value);
class Stream {
 public:
  std::string push(std::string_view chunk);
  // Any incomplete credential prefix at the boundary is suppressed, not emitted.
  std::string finish();
  std::size_t buffered_bytes() const { return pending_.size(); }
 private:
  std::string pending_;
};
}
