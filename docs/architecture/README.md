# Architecture docs

Порядок чтения:

1. [C4 L1 — System Context](c4-context.md) — границы, акторы, внешние
   системы, качества.
2. [C4 L2 — Containers](c4-containers.md) — процессы, библиотеки, кто
   владеет камерой, протоколы, отказы.
3. [C4 L3 — Components](c4-components.md) — модули внутри контейнеров,
   их контракты и зависимости.
4. [C4 L4 — Code](c4-code.md) — классы, последовательности, потоки,
   блокировки, инварианты.
5. [Principles](principles.md) — Clean Architecture, SOLID, KISS, DRY,
   C4, отвергнутые альтернативы, риски.

Решения:

- [ADR 0001](../adr/0001-openvino-npu.md)
- [ADR 0002](../adr/0002-gstreamer-element-and-daemon.md)
- [ADR 0003](../adr/0003-dbus-method-api.md)
- [ADR 0004](../adr/0004-appindicator-ui.md)
- [ADR 0005](../adr/0005-previews-in-element.md)
- [ADR 0006](../adr/0006-performance-budget.md)
- [ADR 0007](../adr/0007-vcam-cutover.md)
- [ADR 0008](../adr/0008-beauty-skin-mask.md)
- [ADR 0009](../adr/0009-on-demand-camera.md)

Правило сопровождения: изменение контракта (D-Bus, GObject-свойства,
caps, формат превью) требует обновления соответствующего уровня C4 и ADR.
