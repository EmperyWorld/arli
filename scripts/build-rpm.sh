#!/usr/bin/bash
# Build the Fedora RPM. Artifacts go to $ARLI_DIST (default ~/arli-dist), never into the repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(tr -d '[:space:]' < "$ROOT/VERSION")"
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Invalid VERSION" >&2; exit 1; }
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
DIST="${ARLI_DIST:-$HOME/arli-dist}"
TOP="$DIST/rpmbuild"
mkdir -p "$TOP"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# Source allowlist: never copy developer state, caches, credentials or build trees.
tar -C "$ROOT" -czf "$TOP/SOURCES/arli-${VER}.tar.gz" \
  --exclude='__pycache__' --exclude='*.pyc' --exclude='*.pyo' \
  --transform "s,^,arli-${VER}/," \
  VERSION README.md LICENSE NOTICE AGENTS.md src qml scripts tests third_party packaging prompts templates docs

rpmbuild --define "_topdir $TOP" --define "arli_version $VER" -bb "$ROOT/packaging/arli.spec"
shopt -s nullglob
packages=("$TOP"/RPMS/*/"arli-${VER}"*.rpm)
[[ ${#packages[@]} == 1 ]] || { echo "Expected one main RPM, found ${#packages[@]}" >&2; exit 1; }
rpm="${packages[0]}"
cp -f "$rpm" "$DIST/"
printf 'RPM %s\n' "$DIST/$(basename "$rpm")"
