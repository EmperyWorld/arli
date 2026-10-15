#pragma once

#include "facade.hpp"

class QQmlEngine;

namespace arli::embed {

// QML root at runtime: ARLI_QML_DIR env → share next to exe → baked ARLI_QML_DIR → /usr/share/arli/qml.
// Only paths that contain Main.qml or Arli/qmldir are accepted (RPM must not use the build-tree bake).
QString qmlImportPath();

void addQmlImportPath(QQmlEngine& engine);

}  // namespace arli::embed
