#!/usr/bin/env bash
# Offline CI: configure → build → ctest → tree law → qmllint → module size.
# No external LLM: tests launch isolated daemons and mock model servers.
# Live model smokes are separate.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-ci}"
SHELL_ON="${ARLI_BUILD_SHELL:-ON}"

cmake -S "$ROOT/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DARLI_BUILD_SHELL="$SHELL_ON" \
  -DARLI_BUILD_TESTS=ON >/dev/null
cmake --build "$BUILD"

(cd "$BUILD" && ctest --output-on-failure)

BUILD_DIR="$BUILD" ARLI_BUILD_SHELL="$SHELL_ON" bash "$ROOT/scripts/check-installed-sdk.sh"

bash "$ROOT/scripts/check-arli-law.sh"

if [[ "$SHELL_ON" == "ON" ]]; then
  bash "$ROOT/scripts/qmllint.sh"
  echo "qmllint: ok"
fi

# Module size law (AGENTS.md): no source file above 300 lines outside generated/third_party.
# Legacy files still above the cap are listed here until they are split (see docs/01-work-plan.md).
ALLOW_OVER='src/daemon/tools.cpp|src/daemon/turn.cpp|src/shell/facade.cpp'
OVER=$(find "$ROOT/src" "$ROOT/tests" -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
  | LC_ALL=C xargs -0 wc -l | awk -v cap=300 '$1 > cap && $2 != "total" {print $2 ":" $1}' \
  | sed "s|^$ROOT/||" | grep -Ev "$ALLOW_OVER" || true)
if [[ -n "$OVER" ]]; then
  echo "module size law: files above 300 lines:" >&2
  echo "$OVER" >&2
  exit 1
fi
echo "module size law: ok"

echo "CI_OK version=$(tr -d '[:space:]' < "$ROOT/VERSION")"
