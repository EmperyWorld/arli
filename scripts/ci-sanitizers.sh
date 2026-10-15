#!/usr/bin/bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

run_suite() {
  local name="$1" option="$2"
  local compiler="${ARLI_SANITIZER_CXX:-}"
  if [[ -z "$compiler" ]] && command -v clang++ >/dev/null 2>&1; then
    compiler="$(command -v clang++)"
  fi
  local suffix=""
  local -a compiler_args=()
  if [[ -n "$compiler" ]]; then
    suffix="-$(basename "$compiler")"
    compiler_args+=("-DCMAKE_CXX_COMPILER=$compiler")
  fi
  local build="$root/build-$name$suffix"
  cmake -S "$root/src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DARLI_BUILD_SHELL=ON -DARLI_BUILD_TESTS=ON "$option=ON" "${compiler_args[@]}"
  cmake --build "$build" -j "$jobs"
  if [[ "$name" == asan ]]; then
    ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1" \
      ctest --test-dir "$build" --output-on-failure -j 1
  else
    TSAN_OPTIONS="halt_on_error=1:history_size=7:ignore_noninstrumented_modules=1:suppressions=$root/scripts/tsan.supp" \
      ctest --test-dir "$build" --output-on-failure -E '^shell-ui$' -j 1
  fi
}

case "${1:-all}" in
  asan) run_suite asan -DARLI_SANITIZE ;;
  tsan) run_suite tsan -DARLI_THREAD_SANITIZE ;;
  all) run_suite asan -DARLI_SANITIZE; run_suite tsan -DARLI_THREAD_SANITIZE ;;
  *) printf 'usage: %s [asan|tsan|all]\n' "$0" >&2; exit 2 ;;
esac
