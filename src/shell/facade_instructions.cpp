#include "facade.hpp"
#include "desk.hpp"

void AgentFacade::saveDialogInstructions(const QString& text) {
  const auto id = dialog_id_;
  if (id <= 0) return;
  if (text.toUtf8().size() > 16384 || text.contains(QChar(0))) {
    showNote(QStringLiteral("Правила диалога"), QStringLiteral("Не более 16 КиБ, без нулевых символов."));
    return;
  }
  desk_->postControl([this, id, value = text.toStdString()] { desk_->runDialogInstructions(id, value); });
}

void Desk::runDialogInstructions(qint64 id, const std::string& text) {
  grpc::ClientContext ctx;
  bearer(ctx);
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
  arli::v1::SetDialogInstructionsRequest req;
  req.set_dialog_id(id); req.set_instructions(text);
  arli::v1::Dialog reply;
  const auto status = stub()->SetDialogInstructions(&ctx, req, &reply);
  onUi([this, id, status, reply] {
    if (!status.ok()) {
      ui_->showNote(QStringLiteral("Правила не сохранены"), QString::fromStdString(status.error_message()));
      return;
    }
    if (ui_->dialog_id_ != id) return;
    ui_->dialog_instructions_ = QString::fromStdString(reply.instructions());
    emit ui_->dialogChanged();
  });
}
