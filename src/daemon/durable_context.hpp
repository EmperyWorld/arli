#pragma once
#include "db.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>

namespace arli {
// Historical metadata only. Never stores tool output, commands, or inferred instructions.
class DurableContext {
 public:
  DurableContext(Database& db, std::int64_t dialog, const std::filesystem::path& root);
  void record(const std::string& tool, const nlohmann::json& args, bool ok, std::int64_t turn);
  std::string render() const;
  nlohmann::json workflow() const;
  void save_workflow(const nlohmann::json&);
 private:
  Database& db_;
  std::int64_t dialog_;
  nlohmann::json state_;
};
}
