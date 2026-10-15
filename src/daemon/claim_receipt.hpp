#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace arli::worker {
// One ordered Session reader owns this cache. No command is executed twice.
// Only the latest payload is retained; older IDs fail closed, including after completion.
class ClaimReceipt {
 public:
  std::optional<bool> lookup(std::int64_t id, const std::string& payload) const {
    if (id > last_) return std::nullopt;
    return id == last_ && payload == payload_ && accepted_;
  }
  void record(std::int64_t id, std::string payload, bool accepted) {
    last_ = id; payload_ = std::move(payload); accepted_ = accepted;
  }
 private:
  std::int64_t last_ = 0;
  std::string payload_;
  bool accepted_ = false;
};
}  // namespace arli::worker
