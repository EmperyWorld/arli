#pragma once
#include "db.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace arli {
// Orchestrator-owned reconciliation, independent of UI requests and LLM health.
class JobMaintenance {
 public:
  explicit JobMaintenance(std::shared_ptr<Database> db);
  ~JobMaintenance();
 private:
  std::shared_ptr<Database> db_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stop_=false;
  std::thread thread_;
};
}
