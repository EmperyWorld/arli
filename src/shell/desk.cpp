#include "facade.hpp"
#include "desk.hpp"

#include "auth.hpp"
#include "paths.hpp"
#include "secrets.hpp"

#include <cstdlib>

Desk::Desk(AgentFacade* ui) : ui_(ui) {
  const char* env = std::getenv("ARLI_LISTEN");
  const std::string endpoint = (env && *env) ? env : arli::kListenEndpoint;
  channel_ = arli::auth::make_channel(endpoint);
  stub_ = arli::v1::Agent::NewStub(channel_);
  token_ = arli::secrets::ui_token();
  job_watch_=std::make_unique<JobLogWatch>(channel_,[this](JobLogSnapshot snapshot){publishJobLog(std::move(snapshot));});
  thread_ = std::thread([this] { loop(); });
  control_thread_ = std::thread([this] { controlLoop(); });
}

Desk::~Desk() {
  shutdown();
}

void Desk::shutdown() {
  {
    std::lock_guard lock(mu_);
    if (stop_) {
      return;
    }
    stop_ = true;
  }
  { std::lock_guard lock(control_mu_); control_stop_ = true; control_queue_.clear(); }
  control_cv_.notify_all();
  job_watch_->shutdown();
  cancelActive();
  cv_.notify_all();
  if (control_thread_.joinable()) control_thread_.join();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Desk::post(std::function<void()> job) {
  {
    std::lock_guard lock(mu_);
    if (stop_) {
      return;
    }
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
}

void Desk::loop() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mu_);
      cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
      if (stop_ && queue_.empty()) {
        return;
      }
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    job();
  }
}

arli::v1::Agent::Stub* Desk::stub() {
  return stub_.get();
}

void Desk::bearer(grpc::ClientContext& ctx) const {
  arli::auth::bearer(ctx, token_);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
}

void Desk::setActive(grpc::ClientContext* ctx) {
  std::lock_guard lock(active_mu_);
  active_ = ctx;
}

void Desk::cancelActive() {
  std::lock_guard lock(active_mu_);
  if (auto* ctx = active_) {
    ctx->TryCancel();
  }
}

bool Desk::follow(qint64 id) const {
  return ui_->followId().load() == id && !ui_->cancelFlag().load();
}

bool Desk::postControl(std::function<void()> job) {
  { std::lock_guard lock(control_mu_);
    if (control_stop_) return false;
    if (control_queue_.size() >= 32) {
      reportControl(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "control queue full"), false);
      return false;
    }
    control_queue_.push_back(std::move(job));
  }
  control_cv_.notify_one();
  return true;
}
void Desk::controlLoop() {
  for (;;) {
    std::function<void()> job;
    { std::unique_lock lock(control_mu_);
      control_cv_.wait(lock, [&] { return control_stop_ || !control_queue_.empty(); });
      if (control_stop_) return;
      job = std::move(control_queue_.front()); control_queue_.pop_front();
    }
    job();
  }
}
void Desk::reportControl(const grpc::Status& status, bool accepted) {
  if (status.ok() && accepted) return;
  const auto message = status.ok() ? QStringLiteral("Запрос устарел или нет активного действия") :
      QString::fromStdString(status.error_message());
  onUi([this, message] { ui_->status_text_ = message; emit ui_->stateChanged(); });
}
