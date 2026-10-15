#include "agent_service.hpp"
#include "llm_models.hpp"

namespace arli {
AgentService::AgentService(std::shared_ptr<Database> db) : db_(std::move(db)) {
  (void)db_->get_settings(); // Register persisted model credential before serving UI RPCs.
  health_thread_ = std::thread([this] {
    std::unique_lock lock(health_mu_);
    while (!health_stop_) {
      const auto generation = health_generation_;
      lock.unlock();
      bool reachable = false;
      try { reachable = llm_health_ok(db_->get_settings()); } catch (...) {}
      lock.lock();
      if (generation == health_generation_) llm_reachable_.store(reachable);
      health_cv_.wait_for(lock, std::chrono::seconds(5), [&] {
        return health_stop_ || health_generation_ != generation;
      });
    }
  });
}
AgentService::~AgentService() {
  { std::lock_guard lock(health_mu_); health_stop_ = true; }
  health_cv_.notify_all();
  if (health_thread_.joinable()) health_thread_.join();
}
void AgentService::refresh_health() {
  { std::lock_guard lock(health_mu_); ++health_generation_; llm_reachable_.store(false); }
  health_cv_.notify_all();
}
}
