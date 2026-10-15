#pragma once
#include "tools.hpp"
#include <map>
namespace arli {
class WorkspaceRules {
 public:
  explicit WorkspaceRules(std::filesystem::path root) : root_(std::move(root)) {}
  struct Update { bool ok = true; bool changed = false; std::string error; };
  Update refresh(const std::vector<std::string>& paths = {});
  Update for_tool(const std::string& tool, const nlohmann::json& args);
  std::string render() const;
 private:
  std::filesystem::path root_;
  std::map<std::string, std::string> files_;
};
}
