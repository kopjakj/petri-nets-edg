#!/usr/bin/env bash
# Compile only; never starts an emulator. External dependencies are explicit.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to Pico SDK 2.3.0 (98a542c1a62fb549ffb5d66a3e5892b06276b670)}"
: "${FREERTOS_KERNEL_PATH:?Set FREERTOS_KERNEL_PATH to Raspberry Pi FreeRTOS 4f7299d6ea746b27a9dd19e87af568e34bd65b15}"
build=${1:-$here/build}
cmake -S "$here/fw" -B "$build" -G Ninja -DPICO_NO_PICOTOOL=1 \
    -DCMAKE_BUILD_TYPE=Release -DDEFER_GROUP_INIT=1 \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" -DFREERTOS_KERNEL_PATH="$FREERTOS_KERNEL_PATH"
cmake --build "$build" --parallel "${BUILD_JOBS:-4}"
sha256sum "$build/study.bin"
