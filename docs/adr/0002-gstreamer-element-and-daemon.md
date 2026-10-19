# ADR 0002 — GStreamer element + owning daemon

**Статус:** принято, 2026-10-19.

## Контекст

До проекта виртуальная камера работала через `v4l2-relayd` с фиксированной
строкой GStreamer (`libcamerasrc ! videoconvert ! gamma ! appsrc ! v4l2sink`).
Relayd не умеет вызывать модели, композитить маску и менять эффекты.
Нужен кадровый путь 1280×720@30 с in-place обработкой и управлением извне.

## Решение

1. **GStreamer-элемент** `npueffects` (`GstVideoFilter`,
   `transform_frame_ip`): RGB in/out, свойства режима/сил, action signal
   превью.
2. **Демон** `npueffectsd`: собирает пайплайн
   `libcamerasrc → npueffects → queue → v4l2sink`, владеет D-Bus и
   детектом активности.
3. Элемент загружается как обычный плагин
   (`~/.local/share/gstreamer-1.0/plugins`); daemon не линкуется с его
   внутренностями.

## Последствия

- Пайплайн явно задаёт RGB caps; libcamera отдаёт 1280×720 напрямую (без
  `videoscale`, иначе появлялись чёрные поля).
- `queue max-size-buffers=2 leaky=downstream` ограничивает задержку.
- Элемент можно тестировать через `gst-launch-1.0` и
  `tests/bench_pipeline.cpp`.
- Демон — единственный владелец `/dev/video60`; старый relayd маскируется
  (ADR 0007).

## Альтернативы

- **Расширить relayd**: он принимает только готовые строки, не умеет
  D-Bus/модели; пришлось бы форкать.
- **Python-приложение**: GIL/скорость.
- **OBS + плагин**: нет NPU-интеграции, второй стек управления.
- **Свой appsrc-апп**: те же функции, но больше кода, чем GstVideoFilter.
