#pragma once
#include "ui_support.hpp"
#include <QQmlComponent>
#include <QQmlApplicationEngine>
#include <QSignalSpy>
namespace arli_ui_test {
inline bool check_dialog_paging(QQuickWindow* window,DialogModel& model){
  model.setAll({{2,"Paged dialog","2026-10-11"}});model.setFilter("");QSignalSpy requested(&model,&DialogModel::pageRequested);
  model.refreshPages();const auto generation=requested.last().at(1).toULongLong();
  model.acceptPage({{2,"Paged dialog","2026-10-11"}},"next",generation);QTest::qWait(50);
  // A search batch normally fills 50 matches; finish that batch before clicking.
  if(model.loading())model.acceptPage({},"next2",generation,QStringLiteral("Temporary error"));
  QTest::qWait(20);auto* button=findItem(window->contentItem(),"loadMoreDialogsButton");
  bool ok=check(button&&button->isVisible()&&button->isEnabled(),"dialog continuation button visible");
  const auto count=requested.count();if(button)click(window,button);QTest::qWait(20);
  ok&=check(requested.count()==count+1&&model.loading(),"dialog continuation button requests page");
  model.acceptPage({},"",generation,QStringLiteral("List changed"),true);QTest::qWait(20);
  ok&=check(button&&button->property("text").toString()==QStringLiteral("Обновить список"),"invalidated list offers explicit refresh");
  model.setAll({});return ok;
}
inline bool check_message_paging(QQuickWindow* window,QObject* desk,MessageModel& model){
  desk->setProperty("askOpen",false);desk->setProperty("confirmOpen",false);desk->setProperty("busy",false);
  model.setAll({{51,"user","Loaded message",0,false}});model.resetPaging(51);QTest::qWait(60);
  auto* button=findItem(window->contentItem(),"loadOlderMessagesButton");QSignalSpy requested(&model,&MessageModel::olderRequested);
  bool ok=check(button&&button->isVisible()&&button->isEnabled(),"older-page button visible");
  if(button)click(window,button);QTest::qWait(20);
  ok&=check(requested.count()==1&&model.loadingOlder()&&button&&!button->isEnabled(),"older-page click starts one request and disables duplicate click");
  model.acceptOlder({{1,"assistant","Older message",0,false}},0,model.pageGeneration());QTest::qWait(20);
  ok&=check(model.messageCount()==2&&!model.hasMore()&&button&&!button->isVisible(),"completed history hides load button");
  return ok;
}
inline bool check_revert_drawer(QQmlApplicationEngine& engine,QQuickWindow* window,const QString& source,QObject* desk){
  QQmlComponent component(&engine,QUrl::fromLocalFile(source+"/qml/TurnDrawer.qml"));
  const QVariantList rows{QVariantMap{{"kind","changeset"},{"changeset","turn-test"},{"text","Сохранён набор правок"}}};
  auto* object=component.createWithInitialProperties({{"width",640},{"steps",rows},{"canRevert",true}});
  auto* drawer=qobject_cast<QQuickItem*>(object);if(!drawer){delete object;return check(false,"revert drawer renders");}
  drawer->setParentItem(window->contentItem());drawer->setZ(1000);QTest::qWait(50);
  QSignalSpy selected(drawer,SIGNAL(revertChangeset(QString)));auto* button=findItem(drawer,"revertChangesetButton");
  bool ok=check(button&&button->isVisible()&&button->isEnabled(),"revert button visible");
  if(button)click(window,button);ok&=check(selected.count()==1&&selected.at(0).at(0).toString()=="turn-test","selected changeset emitted");
  drawer->setProperty("canRevert",false);QTest::qWait(20);ok&=check(button&&!button->isEnabled(),"busy/read-only disables revert");
  delete drawer;
  QQmlComponent taskComponent(&engine,QUrl::fromLocalFile(source+"/qml/TaskCard.qml"));
  auto* task=qobject_cast<QQuickItem*>(taskComponent.createWithInitialProperties({{"width",640},{"desk",QVariant::fromValue(desk)},{"status","failed"},{"errorText","interrupted"},{"taskId",1},{"turnId",55},{"createdAt",""}}));
  ok&=check(task,"crashed task card renders");if(task){task->setParentItem(window->contentItem());task->setZ(1000);desk->setProperty("busy",false);QTest::qWait(50);auto* action=findItem(task,"taskStepsButton");ok&=check(action,"task offers recovery without assistant message");if(action)click(window,action);ok&=check(desk->property("shownTurn").toInt()==55,"task selects original turn");desk->setProperty("shownTurn",0);delete task;}
  return ok;
}
}
