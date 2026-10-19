# Changelog

## Unreleased

- Add `EXTRA_DEPENDS` to the Debian package builder. The local install
  uses it to declare `v4l2-relayd` and `gc2607-camera-stack` as explicit
  dependencies, so apt keeps the v4l2loopback module-load configuration and
  the GC2607 libcamera tuning installed.

- Fix periodic blur FPS drops: the indicator refreshed four previews every
  5 seconds synchronously through the same `EffectProcessor` as live video,
  blocking the streaming thread. Previews now generate only on camera
  activation, menu open, mode/strength change, or explicit refresh, at
  320 px. Measured blur v4l2: 24.1 fps with the old periodic refresh;
  29.9 fps after the fix. Off mode remains 29.9 fps.

- Add a system-wide Debian package builder (`scripts/build-deb.sh`) that
  installs binaries, the GStreamer plugin, user units, the WirePlumber
  rule, the OpenVINO runtime, and model files into FHS locations under
  `/usr` and `/etc`; add a migration helper for the old per-user install.

- Initial public release as **NpuCamFX** under the MIT license; added an
  American-English README with installation, configuration, and
  troubleshooting instructions.

- Fix `GetState.active`: it was forced to false on every poll while the
  camera streamed, so the tray indicator hid itself exactly during camera
  use. `active` now follows the ADR 0009 semantics (true while the physical
  camera streams, false when idle); verified with Camera open/closed.

- On-demand camera: main appsrc pipeline always delivers black frames;
  separate `libcamerasrc → appsink` pipeline starts only while an app holds
  `/dev/video60` and goes READY after 1 s idle. Physical LED is off when idle;
  models stay loaded; 600 ms debounce / 1 s cooldown; 28–30 fps incl. start.
  ADR 0009.

- Украшательства: новая маска кожи (эллипс лица + яркость + 35 точек;
  исключены борода/очки/волосы/губы), bilateral + glow + подъём,
  default smooth 0.7, подменю силы 0.4/0.7/1.0. Контролируемое сравнение:
  лоб +7, щёки +2–3, борода 0; 30 fps. ADR 0008.

## 0.1.0 — 2026-10-19

- GStreamer-элемент `npueffects`: RGB in-place, gamma, background blur,
  skin smoothing, action signal `generate-previews`.
- OpenVINO-адаптеры: MediaPipe Selfie Segmentation 256×256, OMZ
  face-detection-retail-0004, facial-landmarks-35-adas-0002; NPU с CPU
  fallback.
- Демон `npueffectsd`: пайплайн камеры, D-Bus API, детект активности
  `/dev/video60`, превью в `/run/user/<uid>/npu-effects`.
- UI: AppIndicator (SNI + DBusMenu), иконка появляется при активной
  камере, меню с 4 режимами и PNG-превью; окно крупных превью 2×2
  (320×180) с кнопками «Выбрать».
- systemd user units, скрипты build/install/switch/restore/verify/bench,
  загрузчик моделей.
- Cutover: `gc2607-vcam.service` маскируется, `npu-effects.service`
  владеет `/dev/video60`; откат `restore-relayd.sh`.
- Документация: C4 L1–L4, principles, 7 ADR, user guide, evidence.
- Замеры: 30 fps во всех режимах на живой камере; CLI 19/24/36 мс для
  blur/beauty/blur-beauty.
