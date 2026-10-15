#!/usr/bin/env bash
# Offline regression for list-only, file_pass, plan, memory (no LLM).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-ci}"
if [[ ! -x "$BUILD/arli-test-modes" ]]; then
  cmake -S "$ROOT/src" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARLI_BUILD_TESTS=ON >/dev/null
  cmake --build "$BUILD" --target arli-test-modes arli-test-file-pass arli-test-plan arli-test-memory arli-test-edit-stats
fi
"$BUILD/arli-test-modes"
"$BUILD/arli-test-file-pass"
"$BUILD/arli-test-plan"
"$BUILD/arli-test-memory"
"$BUILD/arli-test-edit-stats"
echo "SMOKE_MODES_OK"
