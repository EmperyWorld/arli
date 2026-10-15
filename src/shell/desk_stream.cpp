#include "facade.hpp"
#include "desk.hpp"
#include <QUuid>

void Desk::runSend(qint64 id, const std::string& text) { runStream(id, text, true); }
void Desk::runAttach(qint64 id) { runStream(id, {}, false); }
void Desk::runRevert(qint64 id,const std::string& changeset,qint64 sourceTurn){runStream(id,{},true,changeset,sourceTurn);}
void Desk::runStream(qint64 id, const std::string& text, bool send,const std::string& changeset,qint64 sourceTurn) {
  stream_after_event_id_ = 0; stream_task_id_ = 0;
  const auto request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  auto supports_resume = [&] {
    grpc::ClientContext probe; bearer(probe);
    probe.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(700));
    arli::v1::HealthReply health;
    return stub()->Health(&probe, arli::v1::HealthRequest{}, &health).ok() && health.event_resume_supported();
  };
  const bool resumable = supports_resume();
  grpc::Status status;
  for (int attempt = 0; attempt < 4 && follow(id); ++attempt) {
    if (attempt && (!resumable || !supports_resume())) break;
    grpc::ClientContext ctx;
    bearer(ctx); ctx.set_deadline(std::chrono::system_clock::time_point::max());
    setActive(&ctx);
    std::unique_ptr<grpc::ClientReader<arli::v1::TurnEvent>> reader;
    if (!changeset.empty()) {
      arli::v1::RevertChangesetRequest req;req.set_dialog_id(id);req.set_changeset_id(changeset);req.set_source_turn_id(sourceTurn);req.set_request_id(request_id);req.set_after_event_id(stream_after_event_id_);
      reader=stub()->RevertChangeset(&ctx,req);
    } else if (send) {
      arli::v1::SendRequest req;
      req.set_dialog_id(id); req.set_text(text); req.set_request_id(request_id);
      req.set_after_event_id(stream_after_event_id_);
      reader = stub()->Send(&ctx, req);
    } else {
      arli::v1::AttachTurnRequest req;
      req.set_dialog_id(id); req.set_task_id(stream_task_id_); req.set_after_event_id(stream_after_event_id_);
      reader = stub()->AttachTurn(&ctx, req);
    }
    if (reader) { pump(*reader, ctx, id); status = reader->Finish(); }
    else status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "stream unavailable");
    setActive(nullptr);
    if (status.ok() || !follow(id)) break;
    const auto code = status.error_code();
    if (code != grpc::StatusCode::UNAVAILABLE && code != grpc::StatusCode::DEADLINE_EXCEEDED &&
        code != grpc::StatusCode::CANCELLED) break;
    // Interruptible bounded backoff. Reusing request_id prevents another enqueue
    // even when Send failed before receiving its first event/task ID.
    for (int n = 0; n < (attempt + 1) * 4 && follow(id); ++n)
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  if (follow(id) && !status.ok() && (send || status.error_code() != grpc::StatusCode::NOT_FOUND))
    reportControl(status, false);
  onUi([this, id, reopen = !send && status.error_code() == grpc::StatusCode::NOT_FOUND] {
    if (ui_->followId().load() != id) return;
    ui_->setBusy(false);
    if (reopen) ui_->openDialog(id);
    else post([this] { runRefresh(); });
  });
}
