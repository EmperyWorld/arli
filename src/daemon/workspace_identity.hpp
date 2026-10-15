#pragma once
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <nlohmann/json.hpp>
namespace arli {
struct WorkspaceError:std::runtime_error {using std::runtime_error::runtime_error;};
// Metadata only. Missing roots bind their nearest existing canonical ancestor.
nlohmann::json capture_workspace(const std::filesystem::path& path);
class WorkspaceLease {
 public:
  WorkspaceLease(const std::filesystem::path& path,const std::string& identity);
  ~WorkspaceLease();
  WorkspaceLease(const WorkspaceLease&)=delete;
  WorkspaceLease& operator=(const WorkspaceLease&)=delete;
  void check() const;
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
}
