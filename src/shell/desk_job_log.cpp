#include "facade.hpp"
#include "desk.hpp"
void Desk::startJobLog(const QVariantMap& row){
  job_watch_->start(row.value(QStringLiteral("id")).toLongLong(),row.value(QStringLiteral("dialogId")).toLongLong(),
    row.value(QStringLiteral("taskId")).toLongLong(),row.value(QStringLiteral("command")).toString(),row.value(QStringLiteral("log")).toString());
}
void Desk::stopJobLog(){if(job_watch_)job_watch_->stop();}
void Desk::publishJobLog(JobLogSnapshot snapshot){
  {std::lock_guard lock(job_log_mu_);job_log_latest_=std::move(snapshot);if(job_log_pending_)return;job_log_pending_=true;}
  onUi([this]{
    JobLogSnapshot latest;{std::lock_guard lock(job_log_mu_);latest=std::move(job_log_latest_);job_log_pending_=false;}
    if(!ui_->job_log_selected_||latest.generation!=job_watch_->generation())return;
    ui_->inspector_title_=std::move(latest.title);ui_->inspector_body_=std::move(latest.body);ui_->inspector_diff_.clear();emit ui_->inspectorChanged();
  });
}
