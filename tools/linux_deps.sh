#!/usr/bin/env bash
# Installs the Linux build dependencies on Ubuntu 24.04 (run with sudo).
# Clang 20 comes from apt.llvm.org; the Vulkan SDK is a separate download
# (see README.md) because Ubuntu's packaged Vulkan headers are too old.
set -euo pipefail
if [ "$(id -u)" -ne 0 ]; then
  echo "run with sudo: sudo bash $0" >&2
  exit 1
fi
apt-get update
apt-get install -y wget gnupg lsb-release software-properties-common git python3 xz-utils \
  cmake ninja-build g++-13 libstdc++-13-dev libgtk-3-dev libx11-xcb-dev libxss-dev \
  libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev zenity
if ! command -v clang-20 >/dev/null; then
  tmp="$(mktemp -d)"
  wget -qO "$tmp/llvm.sh" https://apt.llvm.org/llvm.sh
  bash "$tmp/llvm.sh" 20
  rm -rf "$tmp"
fi
apt-get install -y lld-20
echo "Done. clang-20: $(command -v clang-20)"
