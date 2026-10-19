# Принципы проектирования

Документ фиксирует **как** устроен код и **почему** приняты именно такие
решения. Ссылки ведут на конкретные файлы; при изменении кода документ
обязан обновляться.

## 1. Clean Architecture

### Слои и правило зависимостей

```
┌──────────────────────────────────────────────────────────────┐
│ Frameworks & drivers                                         │
│ GStreamer, OpenVINO, OpenCV, GTK/AppIndicator, D-Bus, V4L2   │
├──────────────────────────────────────────────────────────────┤
│ Interface adapters                                           │
│ src/gst, src/daemon, ui, src/openvino, src/imaging, preview  │
├──────────────────────────────────────────────────────────────┤
│ Application                                                  │
│ EffectProcessor (use case)                                   │
├──────────────────────────────────────────────────────────────┤
│ Domain                                                       │
│ types, ports (IPortraitSegmenter, IFaceAnalyzer,             │
│ IEffectRenderer)                                             │
└──────────────────────────────────────────────────────────────┘
```

Стрелки зависимостей направлены **внутрь**. `include/npucam/domain/` не
включает ни OpenVINO, ни OpenCV, ни GStreamer. `EffectProcessor` знает
только порты. Всё внешнее — адаптеры.

| Слой | Где | Можно импортировать |
|---|---|---|
| Domain | `include/npucam/domain` | STL |
| Application | `include/npucam/application`, `src/application` | domain |
| Adapters | `src/openvino`, `src/imaging`, `src/preview` | application, domain, OpenVINO/OpenCV |
| Frameworks | `src/gst`, `src/daemon`, `ui`, `systemd` | всё выше + GStreamer/GLib/GTK/D-Bus |

### Почему это важно здесь

- Модели можно заменить (MediaPipe → MODNet) без изменения GStreamer-кода:
  меняется `SegmenterConfig`/адаптер.
- Рендеринг можно перенести на GPU, не меняя use case.
- UI можно заменить (AppIndicator → Shell extension) без изменения daemon:
  контракт — D-Bus.
- Тесты use case не требуют NPU/камеры: фейковые порты в
  `tests/test_effect_processor.cpp`.

## 2. SOLID

### S — Single Responsibility

| Компонент | Одна ответственность | Файл |
|---|---|---|
| `EffectProcessor` | оркестрация эффектов на кадре | `src/application/effect_processor.cpp` |
| `OpenVinoSegmenter` | портретная матка | `src/openvino/openvino_segmenter.cpp` |
| `OmzFaceAnalyzer` | лица + 35 точек | `src/openvino/omz_face_analyzer.cpp` |
| `CvEffectRenderer` | пиксельная обработка | `src/imaging/cv_effect_renderer.cpp` |
| `PreviewGenerator` | still-превью 4 режимов | `src/preview/preview_generator.cpp` |
| `npueffectsd` | жизненный цикл пайплайна и D-Bus | `src/daemon/main.cpp` |
| `Indicator` | SNI/меню/превью | `ui/npu-effects-indicator.py` |

Ни один из них не совмещает inference и UI, модели и D-Bus, камеру и
меню.

### O — Open/Closed

- Новый сегментер: реализовать `IPortraitSegmenter`, передать в
  `EffectProcessor`; element не меняется (кроме выбора конфига).
- Новый рендерер: реализовать `IEffectRenderer`.
- Новый режим: добавить `EffectMode` и ветку в `CvEffectRenderer`; UI
  получает новый пункт из списка `MODES`.
- Новый потребитель управления: подключиться к D-Bus; daemon не меняется.

### L — Liskov Substitution

- `MediaPipe`/`MODNet` адаптеры возвращают `PortraitMask` одного контракта;
  `EffectProcessor` не знает, какая модель внутри.
- CPU fallback возвращает тот же тип и семантику.
- `CvEffectRenderer` можно заменить GL-рендерером без изменения
  `EffectProcessor`.

### I — Interface Segregation

Три узких порта вместо одного «бога»:

```cpp
class IPortraitSegmenter { virtual bool segment(...) = 0; };
class IFaceAnalyzer      { virtual std::vector<Face> analyze(...) = 0; };
class IEffectRenderer    { virtual void render(...) = 0; };
```

UI видит только 7 D-Bus методов. Element выставляет только нужные
свойства и один signal. Нет общего «модуля эффектов», который тянет всё.

### D — Dependency Inversion

- Application зависит от абстракций, а не от OpenVINO.
- Daemon зависит от GStreamer-контракта (`g_object_set`,
  `generate-previews`), а не от C++-класса элемента.
- UI зависит от D-Bus, а не от daemon-библиотеки.

## 3. KISS

Сознательно выбраны простые решения:

| Решение | Почему |
|---|---|
| Один daemon + один элемент | нет плагинной системы и брокера; меньше состояний |
| D-Bus **методы + polling** | не нужны properties/signals/кэш; UI опрашивает раз в секунду |
| Превью — PNG-файлы | UI читает файл; не нужен общий буфер/сокет |
| UI — AppIndicator | работает без перезапуска сессии; меню и превью через `icon-data` |
| Режимы — enum, не цепочка плагинов | 4 режима, добавление — одна ветка |
| Модели — два адаптера, не фабрика | MediaPipe + OMZ; конфиг вместо иерархии |
| Один `EffectProcessor` | видео и превью идут одним путём |

## 4. DRY

- `EffectSettings` — единственный источник режима/силы; его читают
  элемент, daemon и preview.
- `EffectProcessor` — единственная реализация «что такое blur/beauty»;
  `PreviewGenerator` не дублирует пайплайн.
- `OpenVinoSegmenter` параметризован `SegmenterConfig` и обслуживает и
  MediaPipe, и MODNet.
- D-Bus XML — один контракт для daemon и UI; UI не дублирует список
  методов.
- `scripts/verify.sh` использует те же команды, что и документация.

## 5. C4

Документация явно разложена по уровням:

| Уровень | Документ | Отвечает на вопрос |
|---|---|---|
| L1 Context | [c4-context.md](c4-context.md) | кто пользуется и с какими системами граничим |
| L2 Containers | [c4-containers.md](c4-containers.md) | какие процессы/библиотеки, кто чем владеет |
| L3 Components | [c4-components.md](c4-components.md) | какие модули внутри и их контракты |
| L4 Code | [c4-code.md](c4-code.md) | классы, последовательности, блокировки |

Правило: диаграмма уровня N не показывает детали уровня N+1; детали — в
следующем документе.

## 6. Отвергнутые альтернативы

| Альтернатива | Причина отказа |
|---|---|
| `gstonnxinference` | системный ONNX Runtime использует DNNL/CPU, не NPU; нет generic tensor→mask decoder |
| OpenVINO G-API | не входит в pip-wheel OpenVINO 2026.4.1 |
| Python-обработка 720p | 55+ мс/кадр, не 30 fps |
| OBS + плагин | нет NPU-интеграции, лишний стек и второе управление |
| Свой `camadjust` элемент | relayd зависал на первом кадре; `gamma` уже решал тон |
| `videobalance` для насыщенности | per-channel saturation даёт зелёный оттенок |
| GNOME Shell extension | новая extension не грузится без перезапуска сессии; AppIndicator работает сразу |
| Dynamic plugin architecture | 4 режима, KISS; enum достаточно |

## 7. Ограничения и риски

| Ограничение | Следствие | План |
|---|---|---|
| Матка 256×256 | coarse edges, возможны ложные области (стул/фон) | 3×3 median + EMA; MODNet 384×224 как опция |
| EMA-лаг на быстром движении | край маски отстаёт на несколько кадров | motion-gated alpha (0.2/0.65/1.0) |
| Beauty только по кропу лица | фон/руки не сглаживаются | by design |
| CPU-композитинг | 30 fps достигнут, но запас небольшой | GL-рендерер при 4K/60 |
| NPU для крошечных моделей | 3.5 мс NPU vs 2.7 мс CPU | device=CPU возможен; NPU разгружает CPU |
| OpenVINO C++ из venv | RPATH на `~/npu-venv/.../libs` | системный runtime при упаковке |
| Сцена/DR сенсора | пересвет окна, «бледность» | тюнинг экспозиции/кривой, вне эффектов |
| Beauty-маска эвристическая | чистобритый подбородок/тени/стёкла очков | face parsing в следующей версии; ADR 0008 |
| D-Bus без авторизации | любой процесс сессии меняет режим | приемлемо для локального UI |

## 8. Проверяемость

- `tests/test_effect_processor.cpp` — контракт use case без моделей.
- `tests/bench_pipeline.cpp` — fps пайплайна (video/VCam).
- `scripts/verify.sh` — модели, сервисы, D-Bus, плагин, Python.
- `scripts/bench.sh` — 4 режима на живой камере.
- `docs/evidence/` — локальные кадры и скриншоты (не входят в публичный
  репозиторий).
