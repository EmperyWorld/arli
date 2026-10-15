#pragma once
#include "ui_support.hpp"
#include "models.hpp"
#include <functional>

namespace arli_ui_test {
inline bool runWave(QQuickWindow* window, QObject* desk, MessageModel& chat,
                    const std::function<void(const QString&)>& screenshot, QObject* theme) {
  bool ok = true;
  auto* notice = findItem(window->contentItem(), "copyNotice");
  ok &= check(notice && notice->isVisible(), "copying shows clipboard feedback");
  auto* query = findItem(window->contentItem(), "toolResultSearch");
  auto* view = findItem(window->contentItem(), "toolResultView");
  ok &= check(query && view, "result search controls exist");
  if (query && view) {
    query->setProperty("text", "NEW");
    QTest::qWait(80);
    ok &= check(view->property("selectedText").toString() == "new", "search selects exact text in colored diff");
  }
  auto* expand = findItem(window->contentItem(), "expandToolResult");
  if (expand) click(window, expand);
  auto* dialog = window->findChild<QObject*>("expandedResultDialog");
  ok &= check(dialog && dialog->property("opened").toBool(), "result opens expanded reader");
  screenshot("desktop-expanded-result");
  if (dialog) {
    window->resize(800, 600);
    if (theme) { theme->setProperty("mode", "light"); theme->setProperty("textScale", 1.2); }
    screenshot("compact-expanded-result-light");
    auto* copy = qobject_cast<QQuickItem*>(dialog->findChild<QObject*>("copyToolResultButton"));
    const auto at = copy ? copy->mapToScene(QPointF(0, 0)) : QPointF(-1, -1);
    ok &= check(copy && at.y() >= 0 && at.y() + copy->height() <= window->height(), "expanded result copy stays reachable at 120 percent text in compact window");
    if (theme) { theme->setProperty("mode", "dark"); theme->setProperty("textScale", 1); }
    window->resize(1480, 920);
    QTest::qWait(100);
    QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
    QTest::qWait(80);
    auto* dialogQuery = qobject_cast<QQuickItem*>(dialog->findChild<QObject*>("toolResultSearch"));
    ok &= check(dialogQuery && dialogQuery->hasActiveFocus(), "Ctrl+F searches expanded result instead of opening dialogs");
    desk->setProperty("busy", true);
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(100);
    ok &= check(!dialog->property("opened").toBool() && desk->property("stopCount").toInt() == 0, "Escape closes expanded reader without stopping task");
    desk->setProperty("busy", false);
  }
  const auto first = chat.bodyAt(0), second = chat.bodyAt(1);
  QString longText;
  for (int i = 0; i < 150; ++i) longText += "Строка подробного ответа " + QString::number(i) + "\n";
  chat.setAll({{3, "assistant", longText, 2, false}});
  QTest::qWait(180);
  auto* unfold = findItem(window->contentItem(), "expandMessageButton");
  ok &= check(unfold && unfold->isVisible(), "long reply offers expansion");
  if (unfold) {
    auto* bubble = unfold->parentItem()->parentItem();
    const auto before = bubble->height();
    screenshot("desktop-long-reply-collapsed");
    click(window, unfold);
    QTest::qWait(100);
    ok &= check(bubble->height() > before + 300, "expansion reveals full message without truncation");
  }
  chat.setAll({{1, "user", first, 1, false}, {2, "assistant", second, 1, false}});
  auto* stepsToggle = findItem(window->contentItem(), "stepsButton");
  if (stepsToggle) click(window, stepsToggle);
  auto* inspector = findItem(window->contentItem(), "detailInspector");
  auto* review = findItem(window->contentItem(), "layoutButton_review");
  if (review) click(window, review);
  ok &= check(inspector && inspector->isVisible(), "review layout reopens previously hidden inspector");
  window->resize(1300, 800);
  QTest::qWait(100);
  if (review) click(window, review);
  QTest::qWait(350);
  auto* drawer = window->findChild<QObject*>("inspectorDrawer");
  ok &= check(drawer && drawer->property("opened").toBool(), "review opens inspector drawer at intermediate width");
  if (drawer) QMetaObject::invokeMethod(drawer, "close");
  QTest::qWait(350);
  auto* fileCard = findItem(window->contentItem(), "changedFileCard");
  ok &= check(fileCard && fileCard->isVisible() && fileCard->property("path").toString() == "app.cpp", "changed files show actual path and a visible result link");
  if (fileCard) click(window, fileCard);
  QTest::qWait(350);
  ok &= check(drawer && drawer->property("opened").toBool() && desk->property("selectedTrace").toInt() == 1, "file card selects matching result and opens compact inspector");
  if (drawer) QMetaObject::invokeMethod(drawer, "close");
  QTest::qWait(350);
  window->resize(1480, 920);
  return ok;
}
}
