#!/usr/bin/env bash
# Builds the TikTool Live Captions OBS plugin on macOS (universal).
#
# Prereqs:
#   * Xcode + command line tools
#   * brew install cmake ninja qt@6
#   * obsproject obs-deps macos-universal prebuilt
#
# Set:
#   export QT_DIR=/opt/homebrew/opt/qt@6
#   export OBS_DEPS=$HOME/obs-deps/macos-universal
#   export OBS_SRC=$HOME/obs-studio          # tag v31.0.0
#
# Then:
#   ./ci/build-macos.sh

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

: "${QT_DIR:?Set QT_DIR}"
: "${OBS_DEPS:?Set OBS_DEPS}"
: "${OBS_SRC:?Set OBS_SRC}"

mkdir -p build_macos
cmake -G Ninja -S . -B build_macos \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$QT_DIR;$OBS_DEPS" \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
  -DCMAKE_MODULE_PATH="$OBS_SRC/cmake/Modules" \
  -Dlibobs_DIR="$OBS_SRC/libobs" \
  -Dobs-frontend-api_DIR="$OBS_SRC/UI/obs-frontend-api"
cmake --build build_macos --config RelWithDebInfo --parallel

echo
echo "Built: $(pwd)/build_macos/obs-tiktool-captions.plugin"
echo
echo "Install:"
echo "  cp -R build_macos/obs-tiktool-captions.plugin ~/Library/Application\\ Support/obs-studio/plugins/"
