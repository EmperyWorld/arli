#include "facade.hpp"
#include "desk.hpp"

#include <chrono>

namespace {

QString fromBytes(const std::string& s) { return QString::fromStdString(s); }

}  // namespace

void Desk::runJobs() {
  grpc::ClientContext ctx;
  bearer(ctx);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(700));
  arli::v1::ListJobsReply reply;
  arli::v1::ListJobsRequest request; request.set_include_finished(true);
  if (!stub()->ListJobs(&ctx, request, &reply).ok()) {
    return;
  }
  QVariantList rows;
  for (const auto& job : reply.jobs()) {
    QVariantMap row;
    row.insert(QStringLiteral("id"), static_cast<qint64>(job.id()));
    row.insert(QStringLiteral("pid"), job.pid());
    row.insert(QStringLiteral("dialogId"), static_cast<qint64>(job.dialog_id()));
    row.insert(QStringLiteral("taskId"), static_cast<qint64>(job.task_id()));
    row.insert(QStringLiteral("turnId"), static_cast<qint64>(job.turn_id()));
    row.insert(QStringLiteral("command"), fromBytes(job.command()));
    row.insert(QStringLiteral("status"), fromBytes(job.status()));
    row.insert(QStringLiteral("finishedAt"), fromBytes(job.finished_at()));
    row.insert(QStringLiteral("exitCode"), job.exit_code());
    row.insert(QStringLiteral("exitCodeKnown"), job.exit_code_known());
    row.insert(QStringLiteral("log"), fromBytes(job.log_tail()));
    row.insert(QStringLiteral("workdir"), fromBytes(job.workdir()));
    rows.push_back(row);
  }
  onUi([this, rows = std::move(rows)]() mutable {
    ui_->jobs_ = std::move(rows);
    emit ui_->jobsChanged();
  });
}

void Desk::runStopJob(qint64 id) {
  grpc::ClientContext ctx;
  bearer(ctx);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
  arli::v1::StopJobRequest req;
  req.set_id(id);
  arli::v1::StopJobReply reply;
  const auto status = stub()->StopJob(&ctx, req, &reply);
  if (!status.ok() || !reply.ok()) {
    onUi([this] { ui_->status_text_ = QStringLiteral("Остановка фоновой задачи не подтверждена"); emit ui_->stateChanged(); });
  }
  runJobs();
}
