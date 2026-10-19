# ADR 0004 — AppIndicator UI instead of a Shell extension

**Статус:** принято, 2026-10-19.

## Контекст

Нужен значок в верхней панели GNOME, появляющийся при активной камере, с
контекстным меню и превью эффектов. В системе уже включён
`ubuntu-appindicators@ubuntu.com` и работает `org.kde.StatusNotifierWatcher`.

Была написана GNOME Shell extension (`npu-camera-effects@fstronin`), но
GNOME Shell 50 сканирует каталог расширений **только при старте**;
`gnome-extensions install/enable`, правка `enabled-extensions` и
`ReloadExtension` не загружают новое расширение в текущей сессии
(`ReloadExtension is deprecated and does not work`). Для проверки нужен
logout/login, что прерывает работу пользователя.

## Решение

Основной UI — **StatusNotifierItem/AppIndicator**:

- Python 3 + GTK3 + `AyatanaAppIndicator3`;
- иконка `camera-web-symbolic`, статус `ACTIVE` при активной камере,
  `PASSIVE` при простое (иконка скрывается);
- меню: 4 режима, галочка выбранного, превью 160×90;
- превью передаются как `icon-data` (`ay`, PNG) в DBusMenu — расширение
  `ubuntu-appindicators` рисует их в меню.

Типелиб `gir1.2-ayatanaappindicator3-0.1` в системе отсутствует, поэтому
вендорится в `ui/vendor/` и подключается через `GI_TYPELIB_PATH` в
`npu-effects-ui.service`; runtime-библиотека `libayatana-appindicator3-1`
уже установлена.

## Последствия

- UI работает сразу, без перезапуска Shell.
- Зависимость от `ubuntu-appindicators` (уже включён в Ubuntu).
- GTK3 `Gtk.ImageMenuItem` deprecated, но это единственный способ отдать
  картинку как `icon-data`; предупреждения в журнале допустимы.
- При `hidetopbar` панель скрыта, пока курсор не у верхнего края; это
  поведение пользователя, не системы.
- Native Shell extension не поставляется; при необходимости её можно
  добавить отдельным ADR и проверить после logout/login.

## Альтернативы

- Shell extension: требует logout/login; не проверена в этой сессии.
- Отдельное GTK-окно: не «значок в таскбаре».
- `libappindicator-glib`: в системе нет, потребовалась бы новая зависимость.
