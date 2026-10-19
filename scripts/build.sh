#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${OPENVINO_DIR:-}" ]]; then
    for py in "$HOME/npu-venv/bin/python" python3.12 python3; do
        if ! command -v "$py" >/dev/null 2>&1; then
            continue
        fi
        OPENVINO_DIR="$("$py" -c \
            'import openvino, os; print(os.path.join(os.path.dirname(openvino.__file__), "cmake"))' \
            2>/dev/null || true)"
        if [[ -n "$OPENVINO_DIR" ]]; then
            break
        fi
    done
fi
OPENVINO_DIR="${OPENVINO_DIR:-$HOME/npu-venv/lib/python3.12/site-packages/openvino/cmake}"

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
