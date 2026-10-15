#include "embed.hpp"

#include <QQmlEngine>

#include <cstdlib>
#include <filesystem>

namespace arli::embed {
namespace {

bool looks_like_qml_root(const std::filesystem::path& dir) {
  return std::filesystem::exists(dir / "Main.qml") || std::filesystem::exists(dir / "Arli" / "qmldir");
}

}  // namespace

QString qmlImportPath() {
  if (const char* env = std::getenv("ARLI_QML_DIR"); env && *env) {
    const std::filesystem::path p(env);
    if (looks_like_qml_root(p)) {
      return QString::fromUtf8(env);
    }
  }
  std::error_code ec;
  const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    const auto installed = exe.parent_path().parent_path() / "share" / "arli" / "qml";
    if (looks_like_qml_root(installed)) {
      return QString::fromStdString(installed.string());
    }
  }
#ifdef ARLI_QML_DIR
  {
    const std::filesystem::path baked(ARLI_QML_DIR);
    if (looks_like_qml_root(baked)) {
      return QStringLiteral(ARLI_QML_DIR);
    }
  }
#endif
  return QStringLiteral("/usr/share/arli/qml");
}

void addQmlImportPath(QQmlEngine& engine) {
  engine.addImportPath(qmlImportPath());
}

}  // namespace arli::embed
