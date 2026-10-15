#pragma once

#include "arli.grpc.pb.h"
#include "job_log_watch.hpp"
#include <QVariantMap>

#include <grpcpp/grpcpp.h>

#include <QMetaObject>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

class AgentFacade;

// One channel, independent stream and control queues; GUI never waits on RPC.
class Desk {
 public:
  explicit Desk(AgentFacade* ui);
  ~Desk();

  void post(std::function<void()> job);
  void shutdown();
  bool postControl(std::function<void()> job);
  void reportControl(const grpc::Status& status, bool accepted);
  arli::v1::Agent::Stub* stub();
  void setActive(grpc::ClientContext* ctx);
  void cancelActive();

  void runHealth();
  void runJobs();
  void runStopJob(qint64 id);
  void startJobLog(const QVariantMap& row);
  void stopJobLog();
  void runRefresh();
  void requestDialogPage(QString token,quint64 generation);
  void requestOpen(qint64 id);
  void runOpen(qint64 id, std::uint64_t generation);
  void requestOlder(qint64 id, qint64 before, quint64 generation);
  void runCreate();
  void runDialogInstructions(qint64 id, const std::string& text);
  void runRename(qint64 id, const std::string& title);
  void runDelete(qint64 id);
  void runSaveSettings();
  void runSend(qint64 id, const std::string& text);
  void runAttach(qint64 id);
  void runRevert(qint64 id,const std::string& changeset,qint64 sourceTurn);
  void runTurnTrace(qint64 dialogId, qint64 turnId);
  void runLoadModes(qint64 dialogId);
  void runClearFilePass(qint64 dialogId);
  void runClearPlan(qint64 dialogId);
  void pump(grpc::ClientReader<arli::v1::TurnEvent>& reader, grpc::ClientContext& ctx, qint64 id);

  template <class Fn>
  void onUi(Fn&& fn) {
    auto alive = ui_->alive();
    QMetaObject::invokeMethod(
        ui_, [alive, fn = std::forward<Fn>(fn)]() mutable {
          if (alive->load()) {
            fn();
          }
        },
        Qt::QueuedConnection);
  }

 private:
  std::atomic<std::uint64_t> open_generation_{0};
  void publishJobLog(JobLogSnapshot snapshot);
  std::unique_ptr<JobLogWatch> job_watch_;
  std::mutex job_log_mu_;JobLogSnapshot job_log_latest_;bool job_log_pending_=false;
  void loop();
  void controlLoop();
  void runStream(qint64 id, const std::string& text, bool send,const std::string& changeset={},qint64 sourceTurn=0);
  qint64 stream_after_event_id_ = 0;
  qint64 stream_task_id_ = 0;
  void bearer(grpc::ClientContext& ctx) const;
  bool follow(qint64 id) const;

  AgentFacade* ui_;
  std::thread thread_, control_thread_;
  std::mutex control_mu_;
  std::condition_variable control_cv_;
  std::deque<std::function<void()>> control_queue_;
  bool control_stop_ = false;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stop_ = false;
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<arli::v1::Agent::Stub> stub_;
  std::string token_;
  std::mutex active_mu_;
  grpc::ClientContext* active_ = nullptr;
};
