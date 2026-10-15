#!/usr/bin/bash
# Start the orchestrator if it is not already serving, then open the shell.
# AppImage runtime points AppRun at this file, so the bundle is detected from $0.
set -euo pipefail

self="$(readlink -f "$0")"
bundle="$(cd "$(dirname "$self")/../.." && pwd)"
if [[ -d "$bundle/usr/qml" && -x "$bundle/usr/bin/arli-shell" ]]; then
  export APPDIR="$bundle"
  export PATH="$bundle/usr/bin:${PATH:-}"
  export LD_LIBRARY_PATH="$bundle/usr/lib:${LD_LIBRARY_PATH:-}"
  export QML2_IMPORT_PATH="$bundle/usr/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
  export QT_PLUGIN_PATH="$bundle/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
  export ARLI_AGENT_BIN="$bundle/usr/bin/arli-agent"
fi

prefix_bin=""
if [[ -n "${APPDIR:-}" && -x "${APPDIR}/usr/bin/arli-shell" ]]; then
  prefix_bin="${APPDIR}/usr/bin"
fi

bin() {
  if [[ -n "$prefix_bin" ]]; then
    printf '%s/%s\n' "$prefix_bin" "$1"
  else
    command -v "$1"
  fi
}

probe="$(bin arli-probe || true)"
daemon="$(bin arli-daemon || true)"
shell="$(bin arli-shell || true)"
if [[ -z "$probe" || -z "$daemon" || -z "$shell" ]]; then
  printf 'arli: не найдены arli-probe, arli-daemon или arli-shell в PATH\n' >&2
  exit 1
fi
listen="${ARLI_LISTEN:-127.0.0.1:4792}"

if ! "$probe" "$listen" >/dev/null 2>&1; then
  started=0
  # AppImage must not hand off to a host unit that points at /usr/bin.
  if [[ -z "${APPDIR:-}" ]] && command -v systemctl >/dev/null 2>&1; then
    if systemctl --user start arli-daemon.service >/dev/null 2>&1; then
      started=1
    fi
  fi
  if [[ "$started" -eq 0 ]]; then
    "$daemon" "$listen" >/dev/null 2>&1 &
    disown || true
  fi
  for _ in $(seq 1 50); do
    if "$probe" "$listen" >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done
fi

exec "$shell" "$@"
