#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

fail=0
check() {
    if "$@"; then
        echo "ok: $*"
    else
        echo "FAIL: $*" >&2
        fail=1
    fi
}

check ./build/npucam_tests
check gst-inspect-1.0 npueffects
check python3 -m py_compile ui/npu-effects-indicator.py
for f in selfie_mp_fp16.onnx face-detection-retail-0004.xml \
         face-detection-retail-0004.bin facial-landmarks-35-adas-0002.xml \
         facial-landmarks-35-adas-0002.bin; do
    check test -f "${NPUCAM_MODELS:-$HOME/npu-models}/$f"
done
check systemctl --user is-active --quiet npu-effects.service
check systemctl --user is-active --quiet npu-effects-ui.service
check test -e /dev/video60
check gdbus call --session --dest ru.fstronin.NpuEffects \
    --object-path /ru/fstronin/NpuEffects \
    --method ru.fstronin.NpuEffects.GetState
check bash -c "gdbus call --session --dest org.kde.StatusNotifierWatcher \
    --object-path /StatusNotifierWatcher \
    --method org.freedesktop.DBus.Properties.Get \
    org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems \
    | grep -q npu_camera_effects"

if [[ "${1:-}" == "--fps" ]]; then
    ./scripts/bench.sh
fi

exit "$fail"
