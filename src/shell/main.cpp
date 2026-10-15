#include "embed.hpp"
#include "facade.hpp"
#include "version.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>
#include <QVariantMap>

#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("arli-shell %s\n", arli::kVersion);
    return 0;
  }
  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("EmperyWorld"));
  app.setApplicationName(QStringLiteral("Arli"));
  QQuickStyle::setStyle(QStringLiteral("ArliStyle"));
  QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));

  AgentFacade agent;

  QQmlApplicationEngine engine;
  arli::embed::addQmlImportPath(engine);
  const auto qml_root = arli::embed::qmlImportPath().toStdString();
  QVariantMap initial;
  initial.insert(QStringLiteral("shellAgent"), QVariant::fromValue(static_cast<QObject*>(&agent)));
  engine.setInitialProperties(initial);
  engine.load(QUrl::fromLocalFile(QString::fromStdString((std::filesystem::path(qml_root) / "Main.qml").string())));
  if (engine.rootObjects().isEmpty()) {
    return 1;
  }
  return app.exec();
}
