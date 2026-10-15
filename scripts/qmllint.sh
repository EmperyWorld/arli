#!/usr/bin/env bash
# Lint all shell QML; fail on any warning (Qt 6 qmllint).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QMLLINT=""
for c in qmllint qmllint-qt6 /usr/lib64/qt6/bin/qmllint; do
  if command -v "$c" >/dev/null 2>&1; then QMLLINT="$c"; break; fi
done
if [[ -z "$QMLLINT" ]]; then
  echo "qmllint: not found, skipped" >&2
  exit 0
fi
exec "$QMLLINT" --max-warnings 0 -I "$ROOT/qml" \
  "$ROOT"/qml/*.qml "$ROOT"/qml/Arli/*.qml "$ROOT"/qml/ArliStyle/*.qml
