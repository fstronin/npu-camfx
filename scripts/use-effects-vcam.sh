#!/usr/bin/env bash
set -euo pipefail

MODELS="${NPUCAM_MODELS:-$HOME/npu-models}"
for f in selfie_mp_fp16.onnx face-detection-retail-0004.xml \
         face-detection-retail-0004.bin facial-landmarks-35-adas-0002.xml \
         facial-landmarks-35-adas-0002.bin; do
    if [[ ! -f "$MODELS/$f" ]]; then
        echo "missing model: $MODELS/$f" >&2
        echo "run scripts/download-models.sh" >&2
        exit 1
    fi
done

systemctl --user mask --now gc2607-vcam.service 2>/dev/null || true
systemctl --user disable --now cam-preset.service 2>/dev/null || true
systemctl --user reset-failed npu-effects.service npu-effects-ui.service 2>/dev/null || true
systemctl --user enable --now npu-effects.service npu-effects-ui.service

sleep 2
systemctl --user --no-pager --lines=5 status npu-effects.service npu-effects-ui.service || true
echo
echo "virtual camera: /dev/video60 (npueffectsd)"
echo "control: camera icon in the top bar while an application uses the camera"
