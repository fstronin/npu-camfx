# ADR 0001 — OpenVINO NPU as the inference runtime

**Статус:** принято, 2026-10-19.

## Контекст

На машине есть Intel NPU (Meteor Lake, OpenVINO device `NPU`,
`/dev/accel/accel0`) и установленный Python-пакет OpenVINO 2026.4.1 с
NPU-плагином. Системного `openvino-dev`/C++ SDK нет; pip-wheel содержит
заголовки, библиотеки и CMake-конфиг. Нужен inference из C++ без
Python-прослойки в горячем пути.

## Решение

Использовать **OpenVINO C++ runtime из pip-пакета** (`find_package(OpenVINO)`,
`openvino::runtime`) с device `NPU`; при ошибке компиляции — fallback `CPU`.
Модели:

| Модель | Вход | Выход | Роль |
|---|---|---|---|
| MediaPipe Selfie Segmentation (ONNX) | 1×3×256×256, RGB [0,1] | 1×1×256×256 | матка портрета |
| `face-detection-retail-0004` (OMZ FP16) | 1×3×300×300, BGR [0,255] | 1×1×200×7 | лица |
| `facial-landmarks-35-adas-0002` (OMZ FP16) | 1×3×60×60, BGR [0,255] | 1×70 | 35 точек |

## Последствия

- Бинарники и плагин имеют RPATH на
  `~/npu-venv/lib/python3.12/site-packages/openvino/libs`; переносимость
  ограничена этой машиной.
- Модели — внешние файлы в `~/npu-models`, загрузчик
  `scripts/download-models.sh`.
- NPU для крошечных моделей не всегда быстрее CPU: 3.5 мс против 2.7 мс на
  256×256; NPU выбран для разгрузки CPU и как требование задачи.
- Ошибка NPU не роняет камеру: адаптер компилирует модель на CPU.

## Альтернативы

- **ONNX Runtime**: системная сборка имеет только DNNL/CPU; OpenVINO EP
  отсутствует, NPU недоступен.
- **OpenVINO Python в элементе**: GIL и Python-обработка 720p не дают 30 fps.
- **OpenVINO G-API**: не входит в pip-wheel.
- **MODNet 384×224**: лучше края/тело, но 31 мс только на матку; оставлен
  как опция `SegmenterConfig`, не default.
