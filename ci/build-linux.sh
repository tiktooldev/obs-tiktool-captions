#!/usr/bin/env bash
# Builds the TikTool Live Captions OBS plugin on Linux (Ubuntu 24.04 reference).
#
# Prereqs:
#   sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-websockets-dev
#   git clone --depth 1 --branch v31.0.0 https://github.com/obsproject/obs-studio
#
# Set:
#   export OBS_SRC=$HOME/obs-studio
#
# Then:
#   ./ci/build-linux.sh

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

: "${OBS_SRC:?Set OBS_SRC to your obs-studio checkout (tag v31.0.0)}"

mkdir -p build_linux
cmake -G Ninja -S . -B build_linux \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_MODULE_PATH="$OBS_SRC/cmake/Modules" \
  -Dlibobs_DIR="$OBS_SRC/libobs" \
  -Dobs-frontend-api_DIR="$OBS_SRC/UI/obs-frontend-api"
cmake --build build_linux --config RelWithDebInfo --parallel

echo
echo "Built: $(pwd)/build_linux/obs-tiktool-captions.so"
echo
echo "Install (per-user):"
echo "  mkdir -p ~/.config/obs-studio/plugins/obs-tiktool-captions/{bin/64bit,data}"
echo "  cp build_linux/obs-tiktool-captions.so ~/.config/obs-studio/plugins/obs-tiktool-captions/bin/64bit/"
echo "  cp -R data/* ~/.config/obs-studio/plugins/obs-tiktool-captions/data/"
