#!/usr/bin/bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${ARLI_BUILD_DIR:-$root/build}"
base_url="${ARLI_LIVE_BASE_URL:-http://127.0.0.1:8080/v1}"
model="${ARLI_LIVE_MODEL:-local-agent}"
report_dir="${ARLI_LIVE_REPORT_DIR:-/tmp/arli-qwen-gate}"
mkdir -p "$report_dir"

curl -fsS --connect-timeout 4 --max-time 10 "${base_url%/v1}/health" >/dev/null
for case_name in bugfix multi_file recovery file_integrity; do
  "$build/arli-live-eval" "$base_url" "$model" \
    "$report_dir/$case_name.json" "$case_name" no-revert qwen3
done

printf 'QWEN_LIVE_GATE_OK reports=%s\n' "$report_dir"
