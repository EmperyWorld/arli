#pragma once

#include "db.hpp"
#include "file_pass.hpp"
#include "tools.hpp"

#include "arli.grpc.pb.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace arli {

class PlanState {
 public:
  static PlanState from_row(const PlanRow& row);

  bool active() const { return !steps_.empty(); }
  int current() const { return current_; }
  bool current_failed() const { return failed_; }

  ToolResult set_steps(const nlohmann::json& args);
  ToolResult advance(const FilePassState& pass);
  ToolResult mark_failed(const nlohmann::json& args);
  ToolResult retry();
  ToolResult edit_step(const nlohmann::json& args);

  void fill_snapshot(v1::PlanSnapshot* out) const;
  std::vector<std::string> steps() const { return steps_; }

 private:
  std::vector<std::string> steps_;
  int current_ = 0;
  bool failed_ = false;
};

void fill_turn_event_plan(const PlanState& plan, v1::TurnEvent& ev);

ToolResult plan_gate_tool(const FilePassState& pass, const std::string& tool);

}  // namespace arli
