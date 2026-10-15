#!/usr/bin/env bash
# Live acceptance against the LLM in podman (127.0.0.1:8080). No mocks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
LLM_URL="${ARLI_LLM_URL:-http://127.0.0.1:8080/v1}"
LLM_MODEL="${ARLI_LLM_MODEL:-/models/gemma-4-26B.gguf}"
PORT="${ARLI_SMOKE_PORT:-4795}"
STATE="${ARLI_SMOKE_STATE:-/tmp/arli-live-llm-smoke}"

die() { echo "FAIL $*" >&2; exit 1; }

# Preflight: real LLM from podman stack.
curl -sf --max-time 3 "$LLM_URL/models" >/dev/null || die "LLM not up at $LLM_URL (podman ps?)"

cmake -S "$ROOT/src" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$BUILD" --target arli-daemon arli-agent arli-probe arli-smoke >/dev/null

rm -rf "$STATE"
mkdir -p "$STATE/workspace"
MARKER="arli-live-$(date +%s)"
cat >"$STATE/workspace/index.html" <<EOF
<!DOCTYPE html>
<html lang="ru"><head><meta charset="UTF-8"><title>Live</title></head>
<body><h1 id="title">Before</h1><p>smoke</p></body></html>
EOF

fuser -k "${PORT}/tcp" 2>/dev/null || true
export ARLI_STATE_DIR="$STATE"
export ARLI_AGENT_BIN="$BUILD/arli-agent"
"$BUILD/arli-daemon" "127.0.0.1:${PORT}" &
DPID=$!
cleanup() {
  pkill -P "$DPID" 2>/dev/null || true
  kill "$DPID" 2>/dev/null || true
}
trap cleanup EXIT

for _ in $(seq 1 40); do
  "$BUILD/arli-probe" "127.0.0.1:${PORT}" >/dev/null 2>&1 && break
  sleep 0.1
done
PROBE="$("$BUILD/arli-probe" "127.0.0.1:${PORT}")"
echo "$PROBE" | grep -q 'serving=1' || die "daemon not serving: $PROBE"
echo "$PROBE"

# Point settings at live LLM via smoke binary defaults — override through env on daemon DB:
# arli-smoke sets llm in its grpc SetSettings call.
# Deliberately do NOT say "with path=" — Gemma drops path; harness must autofill.
OUT="$("$BUILD/arli-smoke" "127.0.0.1:${PORT}" \
  "Прочитай index.html и замени текст внутри h1 id=\"title\" (сейчас Before) на ${MARKER}. Используй edit_file." \
  2>&1)" || true
echo "$OUT" | tee "$STATE/smoke.out" | tail -50

LOG="$STATE/turn.log"
[[ -f "$LOG" ]] || die "no turn.log"

grep -q 'tool edit_file ok' "$LOG" || die "no edit_file ok. tools: $(grep '^.*tool ' "$LOG" | tail -30)"
grep -q "$MARKER" "$STATE/workspace/index.html" || die "marker not in index.html"

# If model omitted path, autofill must have fired (or model included path — either ok).
if grep -q 'path отсутствует' "$LOG" && ! grep -q 'path_autofill' "$LOG" && ! grep -q 'tool edit_file ok' "$LOG"; then
  die "path missing without autofill recovery"
fi

echo "LIVE_LLM_OK slice=$(echo "$PROBE" | sed -n 's/.*slice=//p') autofill=$(grep -c path_autofill "$LOG" || true)"
exit 0
