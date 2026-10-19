#!/usr/bin/env bash
set -euo pipefail

DEST="${1:-$HOME/npu-models}"
mkdir -p "$DEST"
cd "$DEST"

fetch() {
    echo "-> $2"
    curl -fL --retry 3 --connect-timeout 15 -o "$2" "$1"
}

fetch "https://huggingface.co/onnx-community/mediapipe_selfie_segmentation/resolve/main/onnx/model_fp16.onnx" \
    "selfie_mp_fp16.onnx"
fetch "https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/face-detection-retail-0004/FP16/face-detection-retail-0004.xml" \
    "face-detection-retail-0004.xml"
fetch "https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/face-detection-retail-0004/FP16/face-detection-retail-0004.bin" \
    "face-detection-retail-0004.bin"
fetch "https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/facial-landmarks-35-adas-0002/FP16/facial-landmarks-35-adas-0002.xml" \
    "facial-landmarks-35-adas-0002.xml"
fetch "https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/facial-landmarks-35-adas-0002/FP16/facial-landmarks-35-adas-0002.bin" \
    "facial-landmarks-35-adas-0002.bin"

if [[ "${2:-}" == "--with-modnet" ]]; then
    fetch "https://huggingface.co/Xenova/modnet/resolve/main/onnx/model_fp16.onnx" \
        "modnet_fp16.onnx"
fi

ls -lh "$DEST"/selfie_mp_fp16.onnx \
    "$DEST"/face-detection-retail-0004.* \
    "$DEST"/facial-landmarks-35-adas-0002.*
