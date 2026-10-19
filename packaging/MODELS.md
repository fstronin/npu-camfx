# Bundled model files

The Debian package bundles the following model files under
`/usr/share/npu-camfx/models/`:

| File | Source | License |
|---|---|---|
| `selfie_mp_fp16.onnx` | [MediaPipe Selfie Segmentation](https://huggingface.co/onnx-community/mediapipe_selfie_segmentation) | Apache-2.0 |
| `face-detection-retail-0004.xml` + `.bin` | [Open Model Zoo](https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/face-detection-retail-0004/FP16/) | Apache-2.0 |
| `facial-landmarks-35-adas-0002.xml` + `.bin` | [Open Model Zoo](https://storage.openvinotoolkit.org/repositories/open_model_zoo/2023.0/models_bin/1/facial-landmarks-35-adas-0002/FP16/) | Apache-2.0 |

The files are downloaded from the URLs in `scripts/download-models.sh`.
The package does not include training data.

The OpenVINO runtime under `/usr/lib/npu-camfx/openvino/` is Apache-2.0.
The complete Apache license text is available at
`/usr/share/common-licenses/Apache-2.0`.
