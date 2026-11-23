#!/usr/bin/env bash
# Packs the Linux player download: dist/BlitzTheLeague-<version>-linux-x64.tar.gz
# Contents: blitz, the SDK shared libraries, README.txt and licenses/. Never any game data.
# The version is blitz/CMakeLists.txt project(VERSION) (the updater compares it with GitHub);
# keep the name ending in -linux-x64.tar.gz: the updater looks for it.
# usage: tools/make_release_linux.sh [<version>] [--no-build]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMAKE_VERSION="$(sed -n 's/^project([a-z0-9]* VERSION \([0-9.]*\).*/\1/p' "$ROOT/blitz/CMakeLists.txt")"
[ -n "$CMAKE_VERSION" ] || { echo "no project(... VERSION x.y.z) in blitz/CMakeLists.txt" >&2; exit 1; }
if [ "${1:-}" = "--no-build" ] || [ -z "${1:-}" ]; then VERSION="$CMAKE_VERSION"; else VERSION="$1"; shift; fi
[ "$VERSION" = "$CMAKE_VERSION" ] || { echo "version $VERSION differs from CMakeLists.txt's $CMAKE_VERSION" >&2; exit 1; }
SDK="$ROOT/deps/rexglue-sdk"
BIN="$ROOT/blitz/out/build/linux-amd64/Release"
NAME="BlitzTheLeague-$VERSION-linux-x64"
STAGE="$ROOT/dist/$NAME"
TAR="$ROOT/dist/$NAME.tar.gz"

[ "${1:-}" = "--no-build" ] || "$ROOT/tools/build_linux.sh" release

rm -rf "$STAGE" "$TAR"
mkdir -p "$STAGE/licenses"
cp "$BIN/blitz" "$STAGE/"
# Shared libraries next to the binary; the SDK sets RPATH=$ORIGIN.
find "$BIN" -maxdepth 1 \( -name '*.so' -o -name '*.so.*' \) -exec cp -P {} "$STAGE/" \;
cp "$ROOT/release/README.txt" "$STAGE/"

cp "$ROOT/LICENSE" "$STAGE/licenses/blitz-recomp.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$STAGE/licenses/"
cp "$SDK/LICENSE" "$STAGE/licenses/rexglue-sdk.txt"
for dir in "$SDK"/thirdparty/*/; do
  lib="$(basename "$dir")"
  for f in "$dir"LICEN[SC]E* "$dir"COPYING*; do
    [ -f "$f" ] || continue
    name="$lib-$(basename "$f")"
    [[ "$name" == *.txt ]] || name="$name.txt"
    cp "$f" "$STAGE/licenses/$name"
  done
done

# Guard: nothing from the game may end up in the download.
if find "$STAGE" -type f \( -iname '*.xex' -o -iname '*.iso' -o -iname '*.mpk' -o -name 'blitz.toml' \) | grep -q .; then
  echo "game or user files in the release" >&2
  exit 1
fi

tar -C "$ROOT/dist" -czf "$TAR" "$NAME"
echo "Release: $TAR ($(du -h "$TAR" | cut -f1))"
