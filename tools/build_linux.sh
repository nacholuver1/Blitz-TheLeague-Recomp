#!/usr/bin/env bash
# Linux build (x86-64). Paths are relative to this file.
#   tools/build_linux.sh sdk              build + install the ReXGlue SDK (patches applied first)
#   tools/build_linux.sh configure        configure the game project
#   tools/build_linux.sh release          build Release (default)
#   tools/build_linux.sh relwithdebinfo   build with debug symbols
# Needs: clang-20, lld-20, cmake, ninja, the Vulkan SDK (source its setup-env.sh)
# and the packages listed in README.md. The game build runs codegen, which reads
# game/default.xex from your own disc.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="$ROOT/deps/rexglue-sdk"
export PATH="$SDK/out/install/linux-amd64/bin:$PATH"
action="${1:-release}"

case "$action" in
  sdk)
    cd "$SDK"
    for p in rexglue-dcbz-128 rexglue-audio-diag rexglue-vsync-relaxed rexglue-ffx-present rexglue-vsync-guest-pacing rexglue-first-press-user0 rexglue-audio-starve-snapshot rexglue-gpu-pacing; do
      # A later patch can change lines an earlier one added, so its reverse check fails once
      # both are in: patches applied here are recorded in a stamp file and skipped after that.
      if grep -qxF "$p" .blitz-applied-patches 2>/dev/null; then
        echo "already applied: $p"
      elif git apply --check "$ROOT/patches/$p.patch" 2>/dev/null; then
        git apply "$ROOT/patches/$p.patch"
        echo "$p" >> .blitz-applied-patches
        echo "applied $p"
      elif git apply --reverse --check "$ROOT/patches/$p.patch" 2>/dev/null; then
        echo "$p" >> .blitz-applied-patches
        echo "already applied: $p"
      else
        echo "patch $p does not apply to this SDK version" >&2
        exit 1
      fi
    done
    # The SDK preset names plain clang; Ubuntu's apt.llvm.org packages install clang-20.
    rm -rf out/build/linux-amd64/CMakeCache.txt out/build/linux-amd64/CMakeFiles
    cmake --preset linux-amd64 -DREXGLUE_BUILD_TESTS=OFF \
      -DCMAKE_C_COMPILER="${CC:-clang-20}" -DCMAKE_CXX_COMPILER="${CXX:-clang++-20}"
    cmake --build out/build/linux-amd64 --config Release --target install
    ;;
  configure)
    cd "$ROOT/blitz"
    cmake --preset linux-amd64
    ;;
  release | relwithdebinfo | debug)
    cd "$ROOT/blitz"
    [ -f out/build/linux-amd64/CMakeCache.txt ] || cmake --preset linux-amd64
    # Fresh checkout: the generated sources are only added at configure time,
    # so run codegen first and configure again before the full build.
    if [ ! -f generated/default/sources.cmake ]; then
      cmake --build --preset "linux-amd64-$action" --target blitz_codegen
      cmake --preset linux-amd64
    fi
    cmake --build --preset "linux-amd64-$action"
    ;;
  *)
    echo "usage: $0 sdk|configure|release|relwithdebinfo" >&2
    exit 1
    ;;
esac
