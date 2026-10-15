#include "facade.hpp"
#include "desk.hpp"

void Desk::requestOpen(qint64 id) {
  const auto generation=++open_generation_;
  ui_->messages_->resetPaging(0);
  post([this,id,generation]{runOpen(id,generation);});
}
void Desk::requestOlder(qint64 id,qint64 before,quint64 generation) {
  const auto selection=open_generation_.load();
  const bool queued=postControl([this,id,before,generation,selection]{
    grpc::ClientContext ctx;bearer(ctx);arli::v1::ListMessagesRequest request;
    request.set_dialog_id(id);request.set_before_id(before);request.set_page_size(50);
    arli::v1::ListMessagesReply reply;const auto status=stub()->ListMessages(&ctx,request,&reply);
    QVector<MessageItem> rows;
    if(status.ok())for(const auto& m:reply.messages())rows.push_back({m.id(),QString::fromStdString(m.role()),QString::fromStdString(m.text()),m.turn_id(),false});
    const auto error=status.ok()?QString():status.error_code()==grpc::StatusCode::UNIMPLEMENTED
      ?QStringLiteral("Этот демон не поддерживает загрузку ранних сообщений")
      :QStringLiteral("Не удалось загрузить историю: ")+QString::fromStdString(status.error_message());
    onUi([this,id,generation,selection,rows=std::move(rows),next=reply.next_before_id(),error]() mutable {
      if(selection!=open_generation_.load()||ui_->dialog_id_!=id)return;
      ui_->messages_->acceptOlder(std::move(rows),next,generation,error);
    });
  });
  if(!queued)ui_->messages_->acceptOlder({},before,generation,QStringLiteral("Очередь запросов занята; повторите загрузку"));
}
