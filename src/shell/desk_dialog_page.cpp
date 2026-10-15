#include "facade.hpp"
#include "desk.hpp"

void Desk::requestDialogPage(QString token,quint64 generation) {
  const bool queued=postControl([this,token=token.toStdString(),generation]{
    grpc::ClientContext ctx;bearer(ctx);arli::v1::ListDialogPageRequest request;
    request.set_page_token(token);request.set_page_size(50);arli::v1::ListDialogPageReply reply;
    auto status=stub()->ListDialogPage(&ctx,request,&reply);bool legacy=false;
    if(status.error_code()==grpc::StatusCode::UNIMPLEMENTED&&token.empty()){
      grpc::ClientContext old;bearer(old);arli::v1::ListDialogsReply oldReply;
      status=stub()->ListDialogs(&old,arli::v1::ListDialogsRequest(),&oldReply);
      if(status.ok()){reply.mutable_dialogs()->Swap(oldReply.mutable_dialogs());legacy=true;}
    }
    QVector<DialogItem> rows;if(status.ok())for(const auto& d:reply.dialogs())rows.push_back({d.id(),QString::fromStdString(d.title()),QString::fromStdString(d.updated_at())});
    const bool changed=status.error_code()==grpc::StatusCode::ABORTED;
    const auto error=status.ok()?QString():changed?QStringLiteral("Список изменился. Обновите его, чтобы продолжить.")
      :QStringLiteral("Не удалось загрузить диалоги: ")+QString::fromStdString(status.error_message());
    onUi([this,rows=std::move(rows),next=QString::fromStdString(reply.next_page_token()),generation,error,changed,legacy]() mutable {
      ui_->dialogs_->acceptPage(std::move(rows),next,generation,error,changed,legacy);
      if(ui_->dialog_id_==0&&ui_->dialogs_->rowCount()>0)ui_->openDialog(ui_->dialogs_->idAt(0));
    });
  });
  if(!queued)ui_->dialogs_->acceptPage({},token,generation,QStringLiteral("Очередь запросов занята; повторите загрузку"));
}
