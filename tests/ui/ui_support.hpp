#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QStringList>
#include <cstdio>

namespace arli_ui_test {
inline QStringList warnings;
inline void messages(QtMsgType type, const QMessageLogContext&, const QString& msg) {
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
    if (!msg.contains("QStandardPaths")) warnings.append(msg);
    std::fprintf(stderr, "%s\n", qPrintable(msg));
  }
}
inline bool check(bool ok, const char* reason) {
  if (!ok) std::fprintf(stderr, "UI assertion: %s\n", reason);
  return ok;
}
inline QQuickItem* findItem(QQuickItem* parent, const QString& name) {
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems())
    if (auto* found = findItem(child, name)) return found;
  return nullptr;
}
inline void click(QQuickWindow* window, QQuickItem* item) {
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
  QTest::qWait(50);
}
}
