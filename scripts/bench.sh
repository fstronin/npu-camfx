#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

if ! systemctl --user is-active --quiet npu-effects.service; then
    echo "npu-effects.service is not active" >&2
    exit 1
fi

for mode in off blur beauty blur-beauty; do
    gdbus call --session --dest ru.fstronin.NpuEffects \
        --object-path /ru/fstronin/NpuEffects \
        --method ru.fstronin.NpuEffects.SetMode "$mode" >/dev/null
    sleep 1
    printf '%-12s ' "$mode"
    ./build/npucam-bench --vcam --seconds 6
done
