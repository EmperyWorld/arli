#pragma once

#include "db.hpp"
#include "tools.hpp"

#include "arli.grpc.pb.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace arli {

// In-turn state (loaded from DB at turn start; hold flag is turn-local only).
class FilePassState {
 public:
  static FilePassState from_row(const FilePassRow& row);

  bool active() const { return !paths_.empty(); }
  bool hold_for_write() const { return hold_; }
  std::string head() const;
  bool is_done(const std::string& path) const;
  bool paths_equal(const std::string& a, const std::string& b) const;

  ToolResult set_paths(const nlohmann::json& args);
  ToolResult clear();
  void on_read_ok(const std::string& path);
  void on_mutation_ok(const std::string& path);
  ToolResult review_without_changes(const nlohmann::json& args);

  void fill_snapshot(v1::FilePassSnapshot* out) const;
  std::vector<std::string> paths() const { return paths_; }
  std::vector<std::string> done() const { return done_; }

 private:
  std::vector<std::string> paths_;
  std::vector<std::string> done_;
  std::vector<std::string> reviewed_;
  std::vector<std::string> read_;
  bool hold_ = false;
};

std::string normalize_pass_path(const std::string& path);

void fill_turn_event_file_pass(const FilePassState& pass, v1::TurnEvent& ev);

ToolResult file_pass_gate_tool(const FilePassState& pass, const std::string& tool, const std::string& args_json);

}  // namespace arli
