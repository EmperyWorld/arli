#pragma once
#include "task_acceptance.hpp"
#include <map>
#include <set>

namespace arli {
// The selected bounded milestone is worker-owned context, not a replacement for user requirements.
class TaskStage {
 public:
  explicit TaskStage(const nlohmann::json& saved = nlohmann::json::object());
  static nlohmann::json schemas();
  ToolResult set(const nlohmann::json& args);
  ToolResult defer(const nlohmann::json& args);
  void observe(const std::string& tool, const nlohmann::json&, const ToolResult&, bool foreground);
  bool active() const { return state_.contains("title"); }
  bool has_deferred() const { return state_.contains("deferred") && !state_["deferred"].empty(); }
  bool passed() const;
  bool code_changed() const { return code_changed_; }
  std::string pending() const;
  std::string final_text(const std::string& pending_edits, bool unverified, bool abandoned) const;
  const nlohmann::json& state() const { return state_; }
  nlohmann::json context() const;
 private:
  nlohmann::json state_ = nlohmann::json::object();
  std::set<std::string> changed_;
  std::set<std::string> passed_commands_;
  bool code_changed_ = false;
};
}
