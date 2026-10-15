#!/usr/bin/env bash
# Quick gate for Э9: embeddable QML + proto install path documented.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
test -f "$ROOT/qml/AgentPage.qml"
test -f "$ROOT/docs/09-emperion.md"
test -f "$ROOT/src/proto/arli.proto"
grep -q 'AgentPage' "$ROOT/qml/Main.qml"
grep -q 'share/arli/proto' "$ROOT/src/CMakeLists.txt"
grep -q 'add_library(arli_embed' "$ROOT/src/CMakeLists.txt"
BUILD="${BUILD_DIR:-$ROOT/build-ci}"
if [[ ! -f "$BUILD/libarli_embed.a" ]]; then
  cmake -S "$ROOT/src" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARLI_BUILD_SHELL=ON >/dev/null
  cmake --build "$BUILD" --target arli_embed arli-shell >/dev/null
fi
test -f "$BUILD/libarli_embed.a"
QMLLINT=""
for c in qmllint qmllint-qt6 /usr/lib64/qt6/bin/qmllint; do
  if command -v "$c" >/dev/null 2>&1; then QMLLINT="$c"; break; fi
done
if [[ -n "$QMLLINT" ]]; then
  "$QMLLINT" -I "$ROOT/qml" "$ROOT/qml/AgentPage.qml"
fi
EMPERION_ROOT="${EMPERION_ROOT:-/data/repos/EmperionOS}"
if [[ -d "$EMPERION_ROOT/src" ]]; then
  test -f "$EMPERION_ROOT/AGENTS.md"
  test -f "$EMPERION_ROOT/qml/AgentPage.qml"
  cmp -s "$ROOT/src/proto/arli.proto" "$EMPERION_ROOT/src/proto/arli.proto"
  grep -q 'add_subdirectory("${EMPERION_ARLI_SRC}" arli-build' "$EMPERION_ROOT/src/CMakeLists.txt"
  grep -q 'target_link_libraries(emperion-shell PRIVATE' "$EMPERION_ROOT/src/CMakeLists.txt"
  grep -q '^  arli_embed$' "$EMPERION_ROOT/src/CMakeLists.txt"
  grep -q 'AgentFacade arli' "$EMPERION_ROOT/src/shell/main.cpp"
  grep -q 'arliAgent' "$EMPERION_ROOT/qml/AgentPage.qml"
fi
echo "EMBED_OK"
