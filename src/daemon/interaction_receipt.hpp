#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
namespace arli::worker {
// A bounded cache retains multiple outstanding replies. An evicted ID is never reapplied.
class InteractionReceipt {
 public:
  std::optional<bool> lookup(std::int64_t id, const std::string& payload) const {
    const auto found = receipts_.find(id);
    if (found != receipts_.end()) return found->second.first == payload && found->second.second;
    if (id <= highest_) return false;
    return std::nullopt;
  }
  void record(std::int64_t id, std::string payload, bool accepted) {
    highest_ = id;
    receipts_.emplace(id, std::make_pair(std::move(payload), accepted));
    if (receipts_.size() > 64) receipts_.erase(receipts_.begin());
  }
 private:
  std::int64_t highest_ = 0;
  std::map<std::int64_t, std::pair<std::string, bool>> receipts_;
};
}
