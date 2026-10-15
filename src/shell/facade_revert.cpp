#include "facade.hpp"
#include "desk.hpp"
#include <QRegularExpression>
void AgentFacade::revertTurn(const QString& changeset){
  if(busy_||read_only_||dialog_id_<=0||shown_turn_<=0)return;
  static const QRegularExpression valid(QStringLiteral("^turn-[0-9a-f]{32}$"));
  if(!valid.match(changeset).hasMatch())return;
  bool selected=false;for(const auto& step:message_steps_){const auto row=step.toMap();if(row.value("kind")==QStringLiteral("changeset")&&row.value("changeset")==changeset)selected=true;}
  if(!selected)return;
  setBusy(true);cancel_flag_.store(false);follow_id_.store(dialog_id_);
  resetStreamChars();trace_->clear();clearConfirm();clearAsk();
  turn_edit_summary_.clear();emit turnStatsChanged();
  const auto id=dialog_id_,source=shown_turn_;
  messages_->add(MessageItem{0,QStringLiteral("user"),QStringLiteral("Откат правок хода ")+QString::number(shown_turn_),0,false});
  shown_turn_=0;message_steps_.clear();emit stepsChanged();
  desk_->post([this,id,source,key=changeset.toStdString()]{desk_->runRevert(id,key,source);});
}
