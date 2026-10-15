#!/usr/bin/env bash
# One live turn: 40 line edits + a read after each (≥80 tool steps, 0 FAIL).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
PORT="${ARLI_LONG_PORT:-4801}"
STATE="${ARLI_LONG_STATE:-/tmp/arli-long-tools}"
LLM_URL="${ARLI_LLM_URL:-http://127.0.0.1:8080/v1}"

die() { echo "FAIL $*" >&2; exit 1; }

curl -sf --max-time 3 "$LLM_URL/models" >/dev/null || die "LLM not up at $LLM_URL"

cmake --build "$BUILD" --target arli-daemon arli-agent arli-probe arli-smoke >/dev/null

rm -rf "$STATE"
mkdir -p "$STATE/workspace"
python3 - <<PY
from pathlib import Path
p = Path("$STATE/workspace/board.txt")
p.write_text("".join(f"{i:02d} KEEP\n" for i in range(1, 41)))
PY

fuser -k "${PORT}/tcp" 2>/dev/null || true
export ARLI_STATE_DIR="$STATE"
export ARLI_AGENT_BIN="$BUILD/arli-agent"
"$BUILD/arli-daemon" "127.0.0.1:${PORT}" >"$STATE/daemon.log" 2>&1 &
DPID=$!
cleanup() { pkill -P "$DPID" 2>/dev/null || true; kill "$DPID" 2>/dev/null || true; }
trap cleanup EXIT

for _ in $(seq 1 50); do
  "$BUILD/arli-probe" "127.0.0.1:${PORT}" >/dev/null 2>&1 && break
  sleep 0.1
done
"$BUILD/arli-probe" "127.0.0.1:${PORT}" | grep -q 'serving=1' || die "daemon down"

PROMPT='Новые файлы не создавать.
Файл board.txt: 40 строк, формат "01 KEEP" … "40 KEEP".
Сделай ровно 40 отдельных edit_file, по одной строке:
для n=1..40 вызови edit_file path=board.txt start_line=n end_line=n new_text="<NN> DONE" где NN — номер с нулём (01…40).
После КАЖДОГО edit_file сразу read_file path=board.txt start_line=n end_line=n и проверь, что строка кончается на DONE.
Не объединяй строки в один edit. Не останавливайся, пока не будет 40 правок.
В конце напиши ГОТОВО.'

"$BUILD/arli-smoke" "127.0.0.1:${PORT}" "$PROMPT" >"$STATE/smoke.out" 2>&1 || true
echo "----- smoke tail -----"
tail -20 "$STATE/smoke.out"

LOG="$STATE/turn.log"
[[ -f "$LOG" ]] || die "no turn.log"
cp -a "$LOG" "$STATE/turn.log.copy"

TOOLS=$(grep -c $'\ttool ' "$LOG" || true)
EDITS=$(grep -c 'tool edit_file ok' "$LOG" || true)
FAILS=$(grep -c 'tool .* FAIL' "$LOG" || true)
DONEC=$(grep -c ' DONE$' "$STATE/workspace/board.txt" || true)
echo "TOOLS=$TOOLS EDITS=$EDITS FAILS=$FAILS DONE_LINES=$DONEC"
grep 'summary tools=' "$LOG" || true

[[ "$FAILS" == "0" ]] || die "tool failures=$FAILS"
[[ "$EDITS" -ge 40 ]] || die "edits_ok=$EDITS < 40"
[[ "$TOOLS" -ge 80 ]] || die "tool steps=$TOOLS < 80"
[[ "$DONEC" -ge 40 ]] || die "board DONE lines=$DONEC"
echo "LONG_TOOLS_OK tools=$TOOLS edits=$EDITS"
exit 0
