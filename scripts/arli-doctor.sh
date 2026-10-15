#!/usr/bin/bash
# Check the local Arli runtime: tools, versions, state-dir permissions, daemon.
set -u
fail=0

ok() { printf 'ok   %s\n' "$1"; }
bad() { printf 'FAIL %s\n' "$1" >&2; fail=1; }
warn() { printf 'warn %s\n' "$1"; }

state="${ARLI_STATE_DIR:-}"
if [[ -z "$state" ]]; then
  if [[ -n "${XDG_DATA_HOME:-}" ]]; then
    state="${XDG_DATA_HOME}/arli"
  else
    state="${HOME}/.local/share/arli"
  fi
fi
listen="${ARLI_LISTEN:-127.0.0.1:4792}"

for cmd in rg bwrap git arli-daemon arli-agent arli-shell arli-probe; do
  if command -v "$cmd" >/dev/null 2>&1; then
    ok "$cmd → $(command -v "$cmd")"
  else
    bad "$cmd not found"
  fi
done

ver_of() {
  local bin="$1"
  command -v "$bin" >/dev/null 2>&1 || return 0
  "$bin" --version 2>/dev/null | awk '{print $2}'
}

dv="$(ver_of arli-daemon)"
av="$(ver_of arli-agent)"
sv="$(ver_of arli-shell)"
pv="$(ver_of arli-probe)"
if [[ -n "$dv" && "$dv" == "$av" && "$dv" == "$sv" && "$dv" == "$pv" ]]; then
  ok "versions match ($dv)"
else
  bad "version mismatch daemon=$dv agent=$av shell=$sv probe=$pv"
fi

if [[ -d "$state" ]]; then
  for f in ui_token internal_token arli.db; do
    p="$state/$f"
    if [[ -f "$p" ]]; then
      mode="$(stat -c '%a' "$p")"
      if [[ "$mode" == "600" ]]; then
        ok "$p mode 0600"
      else
        bad "$p mode $mode (expected 0600)"
      fi
    fi
  done
else
  warn "state dir not created yet: $state"
fi

if command -v arli-probe >/dev/null 2>&1 && arli-probe "$listen" >/dev/null 2>&1; then
  line="$(arli-probe "$listen")"
  ok "daemon serving ($line)"
  running="$(printf '%s\n' "$line" | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^version=/) { split($i, a, "="); print a[2] }}')"
  if [[ -n "$dv" && -n "$running" && "$running" != "$dv" ]]; then
    bad "running daemon version $running != binary $dv"
  fi
else
  port="${listen##*:}"
  if command -v ss >/dev/null 2>&1 && ss -ltn | grep -q ":${port} "; then
    bad "port $port is taken but Health failed"
  else
    warn "daemon is not running on $listen"
  fi
fi

llm="${ARLI_LLM_URL:-http://127.0.0.1:8080/v1/models}"
if command -v curl >/dev/null 2>&1; then
  if curl -fsS --max-time 2 "$llm" >/dev/null 2>&1; then
    ok "LLM reachable ($llm)"
  else
    warn "LLM not reachable ($llm)"
  fi
fi

if [[ "$fail" -eq 0 ]]; then
  printf 'DOCTOR_OK\n'
else
  printf 'DOCTOR_FAIL\n' >&2
fi
exit "$fail"
