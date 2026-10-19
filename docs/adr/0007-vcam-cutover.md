# ADR 0007 — Cutover from v4l2-relayd to npueffectsd

**Статус:** принято, 2026-10-19.

## Контекст

Старый `gc2607-vcam.service` (`v4l2-relayd`) и новый `npu-effects.service`
не могут одновременно владеть `/dev/video60`. У старого юнита
`Restart=always` и system-wide enable-симлинк; после перезапуска
пользовательской сессии он снова стартовал, вытесняя новый daemon через
`Conflicts=`.

## Решение

- `scripts/use-effects-vcam.sh`:
  - `systemctl --user mask --now gc2607-vcam.service` (маска сильнее
    disable и переживает рестарт user manager);
  - `systemctl --user disable --now cam-preset.service`;
  - `enable --now npu-effects.service npu-effects-ui.service`.
- `scripts/restore-relayd.sh`:
  - выключает новые юниты, `unmask` старого, `enable --now
    gc2607-vcam.service cam-preset.service`.
- `npu-effects.service` содержит `Conflicts=gc2607-vcam.service` как
  страховку.
- Gamma 0.85 фиксирована в пайплайне; scene-preset (`cam-preset`) больше
  не управляет камерой.

## Последствия

- Один владелец камеры, нет гонки за устройство.
- Старый стек камеры не удалён; откат одной командой.
- `cam-healthcheck` и старые заметки могут считать relayd выключенным;
  это ожидаемо, пока используется новый стек.
- При logout/login маска сохраняется, новые юниты стартуют.

## Альтернативы

- Два loopback-устройства: лишняя сложность, приложения видят только
  `/dev/video60`.
- Цепочка relayd → effects → loopback: двойная буферизация и задержка.
