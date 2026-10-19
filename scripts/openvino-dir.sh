#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${OPENVINO_DIR:-}" ]]; then
    printf '%s\n' "$OPENVINO_DIR"
    exit 0
fi

for py in "$HOME/npu-venv/bin/python" python3.12 python3; do
    if ! command -v "$py" >/dev/null 2>&1; then
        continue
    fi
    dir="$("$py" -c \
        'import openvino, os; print(os.path.join(os.path.dirname(openvino.__file__), "cmake"))' \
        2>/dev/null || true)"
    if [[ -n "$dir" ]]; then
        printf '%s\n' "$dir"
        exit 0
    fi
done

printf '%s\n' "$HOME/npu-venv/lib/python3.12/site-packages/openvino/cmake"
