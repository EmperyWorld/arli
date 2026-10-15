#!/usr/bin/env bash
# Local dev: configure build tree + compile_commands.json for clangd.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-dev}"
SHELL_ON="${ARLI_BUILD_SHELL:-ON}"

cmake -S "$ROOT/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DARLI_BUILD_SHELL="$SHELL_ON" \
  -DARLI_BUILD_TESTS=ON

cmake --build "$BUILD"

ln -sf "$BUILD/compile_commands.json" "$ROOT/compile_commands.json"
echo "compile_commands.json → $BUILD/compile_commands.json"
echo "Optional: sudo dnf install clang-tools-extra  # clangd for C++ navigation"
