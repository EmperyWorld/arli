#include "facade.hpp"
#include "desk.hpp"

#include <QStringList>

namespace {

QStringList toList(const google::protobuf::RepeatedPtrField<std::string>& in) {
  QStringList out;
  out.reserve(in.size());
  for (const auto& s : in) {
    out.push_back(QString::fromStdString(s));
  }
  return out;
}

}  // namespace

void Desk::runLoadModes(qint64 dialogId) {
  QStringList passPaths;
  QStringList passDone;
  QStringList passReviewed;
  QStringList planSteps;
  int planCurrent = 0;
  bool planFailed = false;

  {
    grpc::ClientContext ctx;
    bearer(ctx);
    arli::v1::GetFilePassRequest freq;
    freq.set_dialog_id(dialogId);
    arli::v1::GetFilePassReply frep;
    if (stub()->GetFilePass(&ctx, freq, &frep).ok()) {
      passPaths = toList(frep.paths());
      passDone = toList(frep.done());
      passReviewed = toList(frep.reviewed());
    }
  }
  {
    grpc::ClientContext ctx;
    bearer(ctx);
    arli::v1::GetPlanRequest preq;
    preq.set_dialog_id(dialogId);
    arli::v1::GetPlanReply prep;
    if (stub()->GetPlan(&ctx, preq, &prep).ok()) {
      planSteps = toList(prep.steps());
      planCurrent = prep.current();
      planFailed = prep.current_failed();
    }
  }

  onUi([this, passPaths = std::move(passPaths), passDone = std::move(passDone), passReviewed = std::move(passReviewed), planSteps = std::move(planSteps),
        planCurrent, planFailed]() {
    ui_->applyFilePass(passPaths, passDone, false, passReviewed);
    ui_->applyPlan(planSteps, planCurrent, planFailed);
  });
}

void Desk::runClearFilePass(qint64 dialogId) {
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::ClearFilePassRequest req;
  req.set_dialog_id(dialogId);
  arli::v1::ClearFilePassReply reply;
  if (!stub()->ClearFilePass(&ctx, req, &reply).ok() || !reply.ok()) {
    return;
  }
  onUi([this]() { ui_->applyFilePass({}, {}, false); });
}

void Desk::runClearPlan(qint64 dialogId) {
  grpc::ClientContext ctx;
  bearer(ctx);
  arli::v1::ClearPlanRequest req;
  req.set_dialog_id(dialogId);
  arli::v1::ClearPlanReply reply;
  if (!stub()->ClearPlan(&ctx, req, &reply).ok() || !reply.ok()) {
    return;
  }
  onUi([this]() { ui_->applyPlan({}, 0, false); });
}
