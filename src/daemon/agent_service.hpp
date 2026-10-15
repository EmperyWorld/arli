#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

namespace arli {

// gRPC `arli.v1.Agent` — the UI-facing service (shell / probe / smoke). Thin: validates,
// touches the DB, delegates lifecycle to OrchestratorHub.
class AgentService final : public v1::Agent::Service {
 public:
  explicit AgentService(std::shared_ptr<Database> db);
  ~AgentService() override;

  grpc::Status Health(grpc::ServerContext*, const v1::HealthRequest*, v1::HealthReply*) override;
  grpc::Status ListDialogs(grpc::ServerContext*, const v1::ListDialogsRequest*, v1::ListDialogsReply*) override;
  grpc::Status ListDialogPage(grpc::ServerContext*, const v1::ListDialogPageRequest*, v1::ListDialogPageReply*) override;
  grpc::Status CreateDialog(grpc::ServerContext*, const v1::CreateDialogRequest*, v1::Dialog*) override;
  grpc::Status SetDialogInstructions(grpc::ServerContext*, const v1::SetDialogInstructionsRequest*, v1::Dialog*) override;
  grpc::Status RenameDialog(grpc::ServerContext*, const v1::RenameDialogRequest*, v1::Dialog*) override;
  grpc::Status DeleteDialog(grpc::ServerContext*, const v1::DeleteDialogRequest*, v1::DeleteDialogReply*) override;
  grpc::Status OpenDialog(grpc::ServerContext*, const v1::OpenDialogRequest*, v1::OpenDialogReply*) override;
  grpc::Status ListMessages(grpc::ServerContext*, const v1::ListMessagesRequest*, v1::ListMessagesReply*) override;
  grpc::Status GetSettings(grpc::ServerContext*, const v1::GetSettingsRequest*, v1::Settings*) override;
  grpc::Status SetSettings(grpc::ServerContext*, const v1::Settings*, v1::Settings*) override;
  grpc::Status Cancel(grpc::ServerContext*, const v1::CancelRequest*, v1::CancelReply*) override;
  grpc::Status HardStop(grpc::ServerContext*, const v1::HardStopRequest*, v1::HardStopReply*) override;
  grpc::Status ListTasks(grpc::ServerContext*, const v1::ListTasksRequest*, v1::ListTasksReply*) override;
  grpc::Status AnswerAskUser(grpc::ServerContext*, const v1::AnswerAskUserRequest*,
                             v1::AnswerAskUserReply*) override;
  grpc::Status ConfirmDanger(grpc::ServerContext*, const v1::ConfirmDangerRequest*,
                             v1::ConfirmDangerReply*) override;
  grpc::Status RevertChangeset(grpc::ServerContext*, const v1::RevertChangesetRequest*, grpc::ServerWriter<v1::TurnEvent>*) override;
  grpc::Status Send(grpc::ServerContext*, const v1::SendRequest*, grpc::ServerWriter<v1::TurnEvent>*) override;
  grpc::Status GetTurnTrace(grpc::ServerContext*, const v1::GetTurnTraceRequest*, v1::GetTurnTraceReply*) override;
  grpc::Status AttachTurn(grpc::ServerContext*, const v1::AttachTurnRequest*,
                          grpc::ServerWriter<v1::TurnEvent>*) override;
  grpc::Status ListJobs(grpc::ServerContext*, const v1::ListJobsRequest*, v1::ListJobsReply*) override;
  grpc::Status WatchJobLog(grpc::ServerContext*, const v1::WatchJobLogRequest*, grpc::ServerWriter<v1::JobLogChunk>*) override;
  grpc::Status StopJob(grpc::ServerContext*, const v1::StopJobRequest*, v1::StopJobReply*) override;
  grpc::Status GetFilePass(grpc::ServerContext*, const v1::GetFilePassRequest*, v1::GetFilePassReply*) override;
  grpc::Status ClearFilePass(grpc::ServerContext*, const v1::ClearFilePassRequest*,
                             v1::ClearFilePassReply*) override;
  grpc::Status GetPlan(grpc::ServerContext*, const v1::GetPlanRequest*, v1::GetPlanReply*) override;
  grpc::Status ClearPlan(grpc::ServerContext*, const v1::ClearPlanRequest*, v1::ClearPlanReply*) override;

 private:
  // Stream live events of `task_id` to `writer` until terminal; `replay` is written first.
  grpc::Status stream_task(grpc::ServerContext* ctx, std::int64_t task_id, std::int64_t after_event_id,
                           grpc::ServerWriter<v1::TurnEvent>* writer);
  std::shared_ptr<Database> db_;
  std::atomic<int> job_log_streams_{0};
  std::atomic<int> turn_streams_{0};
  std::mutex enqueue_mu_;
  std::mutex settings_mu_;
  void refresh_health();
  std::atomic<bool> llm_reachable_{false};
  std::mutex health_mu_;
  std::condition_variable health_cv_;
  bool health_stop_ = false;
  unsigned health_generation_ = 0;
  std::thread health_thread_;
};

}  // namespace arli
