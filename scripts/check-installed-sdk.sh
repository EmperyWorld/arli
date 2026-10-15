#!/usr/bin/env bash
# Installs only the SDK component into a disposable staging prefix, never the host.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-ci}"
for binary in arli-daemon arli-agent arli-mock-llm; do
  [[ -x "$BUILD/$binary" ]] || { echo "SDK check requires $binary; build with ARLI_BUILD_TESTS=ON" >&2; exit 1; }
done
STAGE=$(mktemp -d /tmp/arli-sdk-XXXXXX)
trap 'rm -rf "$STAGE"' EXIT
cmake --install "$BUILD" --prefix "$STAGE/original" --component SDK >/dev/null
mv "$STAGE/original" "$STAGE/relocated"
cp -R "$ROOT/tests/sdk_consumer" "$STAGE/consumer"
# Exported usage requirements must not depend on either checkout or build directory.
if rg -F "$ROOT" "$STAGE/relocated" --glob '*.cmake'; then
  echo "SDK export leaks checkout path" >&2; exit 1
fi
cmake -S "$STAGE/consumer" -B "$STAGE/client-build" -G Ninja \
  -DCMAKE_PREFIX_PATH="$STAGE/relocated" -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON >/dev/null
cmake --build "$STAGE/client-build" >"$STAGE/client-build.log" 2>&1 || { cat "$STAGE/client-build.log"; exit 1; }
"$STAGE/client-build/consumer"
"$STAGE/client-build/daemon-roundtrip" "$BUILD/arli-daemon" "$BUILD/arli-agent" "$BUILD/arli-mock-llm" "$STAGE/daemon-state" || { cat "$STAGE/daemon-state/process.log"; exit 1; }
if [[ "${ARLI_BUILD_SHELL:-ON}" == ON ]]; then
  cmake -S "$STAGE/consumer" -B "$STAGE/embed-build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$STAGE/relocated" -DTEST_EMBED=ON >/dev/null
  cmake --build "$STAGE/embed-build" >"$STAGE/embed-build.log" 2>&1 || { cat "$STAGE/embed-build.log"; exit 1; }
  ARLI_STATE_DIR="$STAGE/state" ARLI_LISTEN=127.0.0.1:1 "$STAGE/embed-build/consumer"
fi
echo "INSTALLED_SDK_OK"
