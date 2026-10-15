#include "models.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>

#include "ui/ui_support.hpp"
#include "ui/ui_wave.hpp"
#include "ui/ui_revert.hpp"
using namespace arli_ui_test;

int main(int argc, char** argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
  QGuiApplication app(argc, argv);
  app.setOrganizationName("ArliTests");
  app.setApplicationName("UI");
  qInstallMessageHandler(messages);
  QQuickStyle::setStyle("ArliStyle");
  QQuickStyle::setFallbackStyle("Basic");
  QQmlApplicationEngine engine;
  const QString source = QStringLiteral(ARLI_UI_SOURCE);
  engine.addImportPath(source + "/qml");
  DialogModel dialogs;
  MessageModel chat;
  TraceModel trace;
  TaskModel tasks;
  dialogs.setAll({{1, "Улучшение проекта", "2026-10-09T12:30:00"},
                  {2, "Проверка сборки", "2026-10-08T15:00:00"}});
  QQmlComponent fixture(&engine, QUrl::fromLocalFile(source + "/tests/ui/PreviewDesk.qml"));
  const auto object = [](QObject* obj) { return QVariant::fromValue(obj); };
  std::unique_ptr<QObject> desk(fixture.createWithInitialProperties({
    {"dialogs", object(&dialogs)}, {"messages", object(&chat)},
    {"trace", object(&trace)}, {"tasks", object(&tasks)}}));
  if (!desk) { std::fprintf(stderr, "%s\n", qPrintable(fixture.errorString())); return 1; }
  engine.setInitialProperties({{"shellAgent", object(desk.get())}});
  engine.load(QUrl::fromLocalFile(source + "/qml/Main.qml"));
  if (engine.rootObjects().isEmpty()) return 1;
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!window) return 1;
  window->show();
  bool ok = true;
  const auto screenshot = [&](const QString& name) {
    QTest::qWait(150);
    const QString dir = qEnvironmentVariable("ARLI_UI_SHOTS");
    if (!dir.isEmpty()) {
      QDir().mkpath(dir);
      ok &= check(window->grabWindow().save(dir + "/" + name + ".png"), "screenshot render");
    }
  };
  window->resize(1480, 920);
  screenshot("desktop-empty");
  auto* divider = findItem(window->contentItem(), "workspaceDivider");
  auto* sidebar = findItem(window->contentItem(), "dialogSidebar");
  ok &= check(divider && sidebar, "workspace resize controls available");
  if (divider && sidebar) {
    const auto point = divider->mapToScene(QPointF(divider->width()/2, divider->height()/2)).toPoint();
    const auto original = sidebar->width();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseMove(window, point + QPoint(40, 0), 80);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point + QPoint(40, 0));
    QTest::qWait(100);
    ok &= check(sidebar->width() > original + 20, "dragging divider actually resizes sidebar");
  }
  chat.setAll({{1, "user", "Исправь ошибку и проверь сборку проекта.", 1, false},
               {2, "assistant", "Готово. Исправлено условие выхода из цикла.\n\n**Проверка:** сборка и тесты прошли.\n\n```cpp\nwhile (running) { process(); }\n```", 1, false}});
  trace.setAll({{"action", "", "edit_file", "{\"path\":\"app.cpp\"}", "", true, 12},
                {"observation", "OK\nФайл обновлён", "edit_file", "", "", true, 12}});
  tasks.setAll({{1, 1, "done", "", "2026-10-09 12:30"}});
  screenshot("desktop-chat");
  auto* codeCopy = findItem(window->contentItem(), "copyCodeButton");
  ok &= check(codeCopy, "fenced code has its own copy action");
  if (codeCopy) click(window, codeCopy);
  ok &= check(desk->property("lastCopied").toString() == "while (running) { process(); }", "copy code preserves exact source without fences");
  desk->setProperty("inspectorBody", "OK\nФайл обновлён");
  desk->setProperty("inspectorDiff", "--- app.cpp\n+++ app.cpp\n@@ -1 +1 @@\n-old\n+new");
  auto* resultTab = findItem(window->contentItem(), "inspectorResultTab");
  if (resultTab) click(window, resultTab);
  desk->setProperty("jobLogSelected", true);QTest::qWait(50);
  ok &= check(resultTab && resultTab->property("checked").toBool(), "selecting job log opens result tab");
  auto* detachLog = findItem(window->contentItem(), "detachJobLog");
  ok &= check(detachLog && detachLog->isVisible(), "job log has an explicit detach control");
  if (detachLog) click(window, detachLog);
  ok &= check(!desk->property("jobLogSelected").toBool(), "detach clears job log selection");
  auto* diffButton = findItem(window->contentItem(), "toolDiffButton");
  ok &= check(diffButton && diffButton->isEnabled(), "diff can be opened separately from stdout");
  if (diffButton) click(window, diffButton);
  screenshot("desktop-diff");
  auto* copyResult = findItem(window->contentItem(), "copyToolResultButton");
  if (copyResult) click(window, copyResult);
  ok &= check(desk->property("lastCopied") == desk->property("inspectorDiff"), "diff copy is exact and excludes stdout");
  ok &= runWave(window, desk.get(), chat, screenshot, engine.singletonInstance<QObject*>("Arli", "Theme"));
  auto* focusLayout = findItem(window->contentItem(), "layoutButton_focus");
  auto* fullLayout = findItem(window->contentItem(), "layoutButton_full");
  ok &= check(focusLayout && fullLayout, "visible workspace layout controls");
  if (focusLayout) click(window, focusLayout);
  ok &= check(sidebar && !sidebar->isVisible(), "focus layout hides sidebar");
  if (fullLayout) click(window, fullLayout);
  ok &= check(sidebar && sidebar->isVisible(), "full layout restores sidebar");
  auto* searchButton = findItem(window->contentItem(), "conversationSearchButton");
  auto* searchInput = findItem(window->contentItem(), "conversationSearchInput");
  auto* searchPanel = findItem(window->contentItem(), "conversationSearch");
  ok &= check(searchButton && searchInput && searchPanel, "conversation search available");
  if (searchButton && searchInput && searchPanel) {
    click(window, searchButton);
    searchInput->setProperty("text", "ПРОВЕР");
    QTest::qWait(240);
    ok &= check(searchPanel->property("matchCount").toInt() == 2, "case insensitive search across user and assistant messages");
    auto* next = findItem(window->contentItem(), "conversationSearchNext");
    if (next) click(window, next);
    ok &= check(searchPanel->property("selectedRow").toInt() == 1, "search next selects second message");
    screenshot("desktop-search");
    QVector<MessageItem> history;
    for (int i = 0; i < 12; ++i) history.push_back({i + 1, "user", i == 0 ? "Скрытая ранняя реплика" : "Обычная реплика", 1, false});
    chat.setAll(history);
    searchInput->setProperty("text", "скрытая");
    QTest::qWait(240);
    ok &= check(searchPanel->property("selectedRow").toInt() == 0 && chat.historyOpen() && chat.rowCount() == 12, "search finds early message outside last ten");
    searchInput->forceActiveFocus();
    searchInput->setProperty("text", "Обычная");
    QTest::qWait(240);
    QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier);
    ok &= check(searchPanel->property("selectedRow").toInt() == 11, "Shift+Enter selects previous search match with wrap");
    QTest::keyClick(window, Qt::Key_Return);
    ok &= check(searchPanel->property("selectedRow").toInt() == 1, "Enter selects next search match with wrap");
    searchInput->setProperty("text", "совпадений нет");
    QTest::qWait(240);
    ok &= check(searchPanel->property("matchCount").toInt() == 0, "unmatched search clears selection");
    auto* closeSearch = findItem(window->contentItem(), "conversationSearchClose");
    if (closeSearch) click(window, closeSearch);
    ok &= check(!chat.historyOpen(), "closing search restores history preference");
    chat.setAll({{1, "user", "Первое сообщение", 1, false}, {2, "assistant", "Ответ Арли", 1, false}});
    auto* copyChat = window->findChild<QObject*>("copyConversationAction");
    ok &= check(copyChat, "conversation copy action exists");
    if (copyChat) QMetaObject::invokeMethod(copyChat, "triggered");
    ok &= check(desk->property("lastCopied").toString() == "Вы:\nПервое сообщение\n\nАрли:\nОтвет Арли", "conversation copy includes all speakers and exact text");
  }
  QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
  QTest::qWait(80);
  auto* commandQuery = findItem(window->contentItem(), "commandQuery");
  auto* commandPalette = findItem(window->contentItem(), "commandPalette");
  ok &= check(commandQuery && commandPalette, "command palette controls available");
  if (commandQuery && commandPalette) {
    commandQuery->setProperty("text", "Остановить ход");
    QTest::qWait(80);
    QTest::keyClick(window, Qt::Key_Return);
    ok &= check(desk->property("stopCount").toInt() == 0 && commandPalette->property("open").toBool(), "idle stop command is disabled");
    QTest::keyClick(window, Qt::Key_Escape);
  }
  desk->setProperty("busy", true);
  QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
  QTest::qWait(80);
  ok &= check(searchPanel && searchPanel->property("opened").toBool(), "keyboard opens conversation search");
  QTest::keyClick(window, Qt::Key_Escape);
  ok &= check(searchPanel && !searchPanel->property("opened").toBool() && desk->property("stopCount").toInt() == 0, "Escape closes search without cancelling task");
  desk->setProperty("busy", false);
  window->resize(800, 600);
  screenshot("compact-chat");
  if (searchButton) click(window, searchButton);
  if (searchInput) searchInput->setProperty("text", "сообщение");
  QTest::qWait(240);
  screenshot("compact-search");
  if (searchPanel) QMetaObject::invokeMethod(searchPanel, "stop");
  auto* conversationMenu = findItem(window->contentItem(), "conversationMenuButton");
  if (conversationMenu) click(window, conversationMenu);
  screenshot("compact-conversation-menu");
  auto* chatMenu = window->findChild<QObject*>("conversationMenu");
  auto* menuContent = chatMenu ? chatMenu->property("contentItem").value<QQuickItem*>() : nullptr;
  ok &= check(menuContent && menuContent->mapToScene(QPointF(0, 0)).y() > 100, "conversation menu opens next to its button");
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
  QTest::qWait(350);
  auto* dialogDrawer = window->findChild<QObject*>("dialogDrawer");
  auto* drawerFilter = dialogDrawer ? qobject_cast<QQuickItem*>(dialogDrawer->findChild<QObject*>("dialogFilterInput")) : nullptr;
  ok &= check(drawerFilter && dialogDrawer->property("opened").toBool(), "keyboard opens and focuses dialog drawer");
  if (drawerFilter) {
    drawerFilter->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Down);
    QTest::keyClick(window, Qt::Key_Return);
    QTest::qWait(350);
    ok &= check(!dialogDrawer->property("opened").toBool(), "keyboard dialog selection closes compact drawer");
  }
  auto* input = findItem(window->contentItem(), "composerInput");
  auto* send = findItem(window->contentItem(), "sendButton");
  ok &= check(input && send, "composer controls available");
  if (!input || !send) return 1;
  desk->setProperty("draft", QString(4001, 'x'));
  QTest::qWait(50);
  ok &= check(input->property("text").toString().size() == 4001, "oversized draft preserved");
  ok &= check(!send->isEnabled(), "oversized draft cannot send");
  input->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Return, Qt::ControlModifier);
  ok &= check(desk->property("sendCount").toInt() == 0, "Ctrl+Enter respects length guard");
  desk->setProperty("draft", "Проверь результат");
  QTest::qWait(50);
  ok &= check(send->isEnabled(), "valid draft send enabled");
  click(window, send);
  ok &= check(desk->property("sendCount").toInt() == 1, "send click invokes facade");
  desk->setProperty("daemonUp", false);
  desk->setProperty("draft", "Черновик");
  QTest::qWait(50);
  ok &= check(!send->isEnabled(), "offline send disabled");
  desk->setProperty("daemonUp", true);
  desk->setProperty("draft", "");
  input->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Question);
  ok &= check(!desk->property("helpOpen").toBool(), "typing question mark keeps help closed");
  desk->setProperty("draft", "");
  auto* navigation = findItem(window->contentItem(), "pageButton1");
  ok &= check(navigation, "navigation available without sidebar");
  if (navigation) click(window, navigation);
  ok &= check(desk->property("page").toInt() == 1, "settings navigation invokes facade");
  screenshot("compact-settings");
  for (int section = 1; section < 4; ++section) {
    auto* button = findItem(window->contentItem(), "settingsSection" + QString::number(section));
    ok &= check(button, "settings section available");
    if (button) click(window, button);
    screenshot("compact-settings-" + QString::number(section));
  }
  auto* appearance = findItem(window->contentItem(), "settingsSection4");
  ok &= check(appearance, "appearance section available");
  if (appearance) click(window, appearance);
  auto* light = findItem(window->contentItem(), "lightThemeButton");
  ok &= check(light, "light theme control available");
  if (light) click(window, light);
  auto* theme = engine.singletonInstance<QObject*>("Arli", "Theme");
  ok &= check(theme, "appearance singleton available");
  if (theme) theme->setProperty("textScale", 1.2);
  screenshot("compact-appearance-light");
  desk->setProperty("page", 0);
  window->resize(1480, 920);
  screenshot("desktop-light");
  desk->setProperty("page", 1);
  auto* dark = findItem(window->contentItem(), "darkThemeButton");
  if (dark) click(window, dark);
  if (theme) theme->setProperty("textScale", 1.0);
  window->resize(800, 600);
  desk->setProperty("page", 0);
  desk->setProperty("busy", true);
  QTest::qWait(50);
  auto* hardStop = findItem(window->contentItem(), "hardStopButton");
  if (hardStop) click(window, hardStop);
  auto* hardDialog = window->findChild<QObject*>("hardStopDialog");
  ok &= check(hardDialog && hardDialog->property("visible").toBool(), "hard stop asks for confirmation");
  ok &= check(desk->property("hardStopCount").toInt() == 0, "hard stop not run before approval");
  if (hardDialog) QMetaObject::invokeMethod(hardDialog, "reject");
  QTest::qWait(100);
  if (hardStop) click(window, hardStop);
  QTest::qWait(350);
  auto* approval = findItem(window->contentItem(), "hardStopApprove");
  ok &= check(approval, "hard stop approval available");
  if (approval) click(window, approval);
  ok &= check(desk->property("hardStopCount").toInt() == 1, "hard stop runs once after approval");
  QTest::qWait(100);
  const QPointF sendPosition = send->mapToScene(QPointF(0, 0));
  ok &= check(sendPosition.y() >= 0 && sendPosition.y() + send->height() <= window->height(), "composer stays within compact window");
  desk->setProperty("confirmOpen", true);
  desk->setProperty("confirmKind", "shell");
  desk->setProperty("confirmCommand", "git status --short\n" + QString(1000, 'a'));
  desk->setProperty("confirmConsequence", "Команда будет выполнена в рабочей папке.");
  screenshot("compact-confirmation");
  if (theme) theme->setProperty("textScale", 1.2);
  screenshot("compact-confirmation-large-text");
  const QPointF confirmSendPosition = send->mapToScene(QPointF(0, 0));
  ok &= check(confirmSendPosition.y() + send->height() <= window->height(), "confirmation keeps composer reachable");
  if (theme) theme->setProperty("textScale", 1.0);
  window->resize(1480, 920);
  screenshot("desktop-confirmation");
  desk->setProperty("confirmOpen", false);
  desk->setProperty("askOpen", true);
  desk->setProperty("askQuestion", "Какой вариант выбрать?");
  desk->setProperty("askOptions", QStringList{"Исправить код", "Сначала объяснить"});
  screenshot("desktop-question");
  ok &= check_revert_drawer(engine,window,source,desk.get());
  ok &= check_message_paging(window,desk.get(),chat);
  ok &= check_dialog_paging(window,dialogs);
  ok &= check(warnings.isEmpty(), "QML produced no runtime warnings");
  if (ok) std::puts("UI_OK: responsive rendering, composer guards, keyboard and facade actions");
  delete window;
  return ok ? 0 : 1;
}
