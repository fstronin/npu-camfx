#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

OPENVINO_DIR="$("$PWD/scripts/openvino-dir.sh")"

if [[ ! -d "$OPENVINO_DIR" ]]; then
    echo "OpenVINO CMake package not found: $OPENVINO_DIR" >&2
    echo "Install the OpenVINO wheel (see README) or set OPENVINO_DIR." >&2
    exit 1
fi

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${PREFIX:-$HOME/.local}" \
    -DOpenVINO_DIR="$OPENVINO_DIR"
cmake --build build -j"$(nproc)"
