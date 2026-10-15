#!/usr/bin/env bash
# Э2/Э8 lifecycle on mock LLM (daemon + agent + arli-mock-llm).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-ci}"
if [[ ! -x "$BUILD/arli-test-lifecycle" ]]; then
  cmake -S "$ROOT/src" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DARLI_BUILD_TESTS=ON >/dev/null
  cmake --build "$BUILD" --target arli-daemon arli-agent arli-mock-llm arli-test-lifecycle
fi
(cd "$BUILD" && ctest -R '^lifecycle$' --output-on-failure)
echo "SMOKE_LIFECYCLE_OK"
