#pragma once

#include "db.hpp"
#include "tools.hpp"

#include "arli.grpc.pb.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace arli {

inline constexpr int kMemoryInjectLimit = 20;
inline constexpr int kMemoryStoreLimit = 200;

class MemoryState {
 public:
  static MemoryState load(const std::vector<MemoryRow>& rows, std::int64_t next_id = 1);

  std::string inject_block() const;
  ToolResult remember(const nlohmann::json& args);
  ToolResult forget(const nlohmann::json& args);
  ToolResult list() const;

  void fill_snapshot(v1::MemorySnapshot* out) const;
  const std::vector<MemoryRow>& entries() const { return entries_; }

 private:
  std::vector<MemoryRow> entries_;
  std::int64_t next_id_ = 1;
};

void fill_turn_event_memory(const MemoryState& mem, v1::TurnEvent& ev);

std::vector<MemoryRow> memory_rows_for_inject(Database& db);

}  // namespace arli
