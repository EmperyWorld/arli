#!/usr/bin/env bash
# Build and run daemon + shell for local development.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
export ARLI_QML_DIR="${ARLI_QML_DIR:-$ROOT/qml}"
export ARLI_STATE_DIR="${ARLI_STATE_DIR:-$HOME/.local/share/arli-dev}"
export ARLI_LISTEN="${ARLI_LISTEN:-127.0.0.1:4792}"
export ARLI_AGENT_BIN="${ARLI_AGENT_BIN:-$BUILD/arli-agent}"

mkdir -p "$BUILD" "$ARLI_STATE_DIR"
cmake -S "$ROOT/src" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DARLI_QML_DIR="$ARLI_QML_DIR"
cmake --build "$BUILD"

bash "$ROOT/scripts/check-arli-law.sh"

# Free the previous stack. fuser kills the listener; the worker is only a client, so it
# survives and reconnects into the new daemon (Session already taken → reconnect storm).
PORT="${ARLI_LISTEN##*:}"
if command -v fuser >/dev/null 2>&1; then
  fuser -k "${PORT}/tcp" >/dev/null 2>&1 || true
fi
pkill -x arli-agent >/dev/null 2>&1 || true
sleep 0.2

"$BUILD/arli-daemon" "$ARLI_LISTEN" &
DAEMON_PID=$!
cleanup() {
  pkill -P "$DAEMON_PID" 2>/dev/null || true
  kill "$DAEMON_PID" 2>/dev/null || true
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  if "$BUILD/arli-probe" "$ARLI_LISTEN" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
"$BUILD/arli-probe" "$ARLI_LISTEN"

exec "$BUILD/arli-shell"
