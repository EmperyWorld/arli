#!/usr/bin/bash
# Bundle daemon + worker + shell into an AppImage.
# Tools are downloaded into $ARLI_DIST/tools. The AppImage itself is not stored in the repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(tr -d '[:space:]' < "$ROOT/VERSION")"
DIST="${ARLI_DIST:-$HOME/arli-dist}"
TOOLS="$DIST/tools"
APPDIR="$DIST/AppDir"
mkdir -p "$TOOLS" "$DIST"

fetch() {
  local url="$1" dest="$2"
  if [[ -x "$dest" ]]; then
    return 0
  fi
  curl -fL --retry 3 -o "$dest" "$url"
  chmod +x "$dest"
}

fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
  "$TOOLS/appimagetool-x86_64.AppImage"

BUILD="$DIST/appimage-build"
rm -rf "$APPDIR"
cmake -S "$ROOT/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DARLI_BUILD_SHELL=ON \
  -DARLI_BUILD_TESTS=OFF
cmake --build "$BUILD"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

# Qt from the system, copied as files. No patchelf: RELR binaries break when their rpath is rewritten.
QT_QML=/usr/lib64/qt6/qml
QT_PLUGINS=/usr/lib64/qt6/plugins
mkdir -p "$APPDIR/usr/qml/QtQuick" "$APPDIR/usr/plugins/platforms" "$APPDIR/usr/plugins/xcbglintegrations"
cp -a "$QT_QML/QtQuick/qmldir" "$QT_QML/QtQuick/plugins.qmltypes" "$QT_QML/QtQuick/"*.so "$APPDIR/usr/qml/QtQuick/"
for module in Controls Layouts Templates Window; do
  cp -a "$QT_QML/QtQuick/$module" "$APPDIR/usr/qml/QtQuick/"
done
cp -a "$QT_QML/QtQml" "$APPDIR/usr/qml/"
cp -a "$QT_PLUGINS/platforms/libqxcb.so" "$QT_PLUGINS/platforms/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
if [[ -d "$QT_PLUGINS/xcbglintegrations" ]]; then
  cp -a "$QT_PLUGINS/xcbglintegrations/." "$APPDIR/usr/plugins/xcbglintegrations/"
fi
python3 "$ROOT/scripts/bundle-appimage-libs.py" "$APPDIR"

cp "$ROOT/packaging/arli.sh" "$APPDIR/usr/bin/arli"
chmod +x "$APPDIR/usr/bin/arli"
cp "$ROOT/packaging/arli.desktop" "$APPDIR/arli.desktop"
cp "$ROOT/packaging/arli.svg" "$APPDIR/arli.svg"
# appimagetool turns AppRun into a symlink to the desktop Exec= binary.
cp "$APPDIR/usr/bin/arli" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"

export APPIMAGE_EXTRACT_AND_RUN=1
out="$DIST/Arli-${VER}-x86_64.AppImage"
ARCH=x86_64 "$TOOLS/appimagetool-x86_64.AppImage" --appimage-extract-and-run "$APPDIR" "$out"
chmod +x "$out"
rm -f "$ROOT"/*.AppImage
printf 'APPIMAGE %s\n' "$out"
