#pragma once
#include "llm.hpp"
#include "tools.hpp"
#include <filesystem>
#include <map>
#include <set>
namespace arli {
class TaskAcceptance {
 public:
  static TaskAcceptance prepare(const LlmConfig&, const std::string& request, std::atomic<bool>* cancel);
  static TaskAcceptance from_json(const nlohmann::json&, const std::string& request = {});
  void mutation() { ++revision_; }
  std::string evaluate(const std::filesystem::path& root);
  std::string before_tool(const std::filesystem::path&,const std::string&,bool mutating) const;
  void observe(const std::filesystem::path&,const std::string&,const nlohmann::json&,const ToolResult&,bool foreground_shell_ran=false,bool shell_refused=false);
  bool blocked() const { for(const auto& [command,revision]:commands_)if(blocked_commands_.contains(command))return true;return false; }
  bool command_blocked(const std::string& command) const { return blocked_commands_.contains(command); }
  bool has_checks() const { return !plan_["checks"].empty(); }
  std::string state() const;
  std::string note() const { return note_; }
  std::string serialized() const { return plan_.dump(); }
  std::int64_t checked_revision() const { return checked_revision_; }
 private:
  nlohmann::json plan_ = {{"checks", nlohmann::json::array()}, {"unverifiable", "Нет проверяемых критериев; результат требует ручной проверки."}};
  std::string note_ = "Нет проверяемых критериев; результат требует ручной проверки.";
  std::string failure_;
  std::int64_t revision_ = 0, checked_revision_ = -1;
  struct ReadState {bool pending=false;std::string hash;int total=-1;std::set<int> lines;};
  std::map<std::string,ReadState> rereads_;
  std::map<std::string,std::int64_t> commands_;
  std::set<std::string> blocked_commands_;
};
}
