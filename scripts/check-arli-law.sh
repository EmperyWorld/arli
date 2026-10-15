#!/bin/sh
# Законы дерева Arli.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"

fail() {
  printf 'arli-law: %s\n' "$1" >&2
  exit 1
}

if find . \
  -path './.git' -prune -o \
  -path './build' -prune -o \
  -path './build-*' -prune -o \
  -path './out' -prune -o \
  -path './third_party' -prune -o \
  \( -name '*.so' -o -name '*.so.*' -o -name '*.AppImage' \) -print \
  | grep -q .; then
  fail 'в дереве лежит бинарный артефакт (so, AppImage)'
fi

if [ -f package.json ] || [ -f qml/package.json ] || [ -f src/package.json ]; then
  fail 'package.json: веб-тулчейн в продукт не входит'
fi

scan_dirs=
for d in src qml; do
  if [ -d "$d" ]; then
    scan_dirs="$scan_dirs $d"
  fi
done

if [ -n "$scan_dirs" ]; then
  if grep -R -n -E 'org\.kde|Kirigami|kirigami|WebEngine|QWebEngine' $scan_dirs >/dev/null 2>&1; then
    fail 'Kirigami/WebEngine в коде продукта'
  fi
  if grep -R -n -E '0\.0\.0\.0' $scan_dirs >/dev/null 2>&1; then
    fail 'широкий bind (0.0.0.0) в коде продукта'
  fi
  if grep -R -n -E 'xdg-open|QDesktopServices::openUrl' $scan_dirs >/dev/null 2>&1; then
    fail 'открытие системного браузера как оболочки запрещено'
  fi
  # Zero-trust loopback: only local credentials + bearer metadata (src/common/auth.cpp).
  if grep -R -n -E 'Insecure(Server|Channel)Credentials' $scan_dirs >/dev/null 2>&1; then
    fail 'InsecureServerCredentials/InsecureChannelCredentials в коде продукта — используй arli::auth'
  fi
  if grep -R -n -E 'getenv\("ARLI_INTERNAL_TOKEN"\)' src/shell >/dev/null 2>&1; then
    fail 'оболочка не должна знать internal token'
  fi
fi

if [ ! -f VERSION ] || ! grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' VERSION; then
  fail 'VERSION должен быть MAJOR.MINOR.PATCH'
fi

printf 'arli-law: ok\n'
