#!/usr/bin/env python3
"""AppIndicator control for npueffectsd.

This is the GNOME top-bar control available without restarting the session:
Ubuntu's appindicators extension renders the StatusNotifierItem, and the menu
shows a live thumbnail for every effect mode. All inference stays in the
daemon; this process only calls the D-Bus API.

Run with the vendored typelib when gir1.2-ayatanaappindicator3-0.1 is not
installed system-wide:

    GI_TYPELIB_PATH=.../girepository-1.0 ./npu-effects-indicator.py
"""
import argparse
import os
import signal
import sys

import gi

gi.require_version('Gtk', '3.0')
gi.require_version('AyatanaAppIndicator3', '0.1')
from gi.repository import AyatanaAppIndicator3 as AppIndicator  # noqa: E402
from gi.repository import GdkPixbuf, Gio, GLib, Gtk  # noqa: E402

SERVICE = 'ru.fstronin.NpuEffects'
OBJECT_PATH = '/ru/fstronin/NpuEffects'
INTERFACE = 'ru.fstronin.NpuEffects'
MODES = [
    ('off', 'Без эффектов'),
    ('blur', 'Размытие фона'),
    ('beauty', 'Украшательства'),
    ('blur-beauty', 'Размытие + украшательства'),
]
PREVIEW_WIDTH = 160
PREVIEW_HEIGHT = 90


def value(v):
    if isinstance(v, GLib.Variant):
        return v.unpack()
    return v


class Indicator:
    def __init__(self):
        self.proxy = Gio.DBusProxy.new_for_bus_sync(
            Gio.BusType.SESSION,
            Gio.DBusProxyFlags.DO_NOT_AUTO_START,
            None, SERVICE, OBJECT_PATH, INTERFACE, None)
        self.mode = 'off'
        self.active = False
        self.shown = False
        self.items = {}
        self.preview_paths = {}
        self.ticks = 0
        self.window = None
        self.window_images = {}
        self.strength_items = {}
        self.smooth = 0.7
        self._updating_strength = False

        self.indicator = AppIndicator.Indicator.new(
            'npu-camera-effects', 'camera-web-symbolic',
            AppIndicator.IndicatorCategory.APPLICATION_STATUS)
        self.indicator.set_title('NpuCamFX')
        self.indicator.set_status(AppIndicator.IndicatorStatus.PASSIVE)

        self.menu = Gtk.Menu()
        self.menu.connect('show', self._on_menu_show)
        self._build_menu()
        self.indicator.set_menu(self.menu)

        self._poll()
        if self.active:
            self._refresh_previews()
        GLib.timeout_add_seconds(1, self._poll)
        print('indicator ready', flush=True)

    def _build_menu(self):
        header = Gtk.MenuItem(label='Эффекты камеры (NPU)')
        header.set_sensitive(False)
        self.menu.append(header)
        self.menu.append(Gtk.SeparatorMenuItem())

        for mode, label in MODES:
            # Gtk.ImageMenuItem is deprecated but is the only menu item whose
            # image the Ayatana DBusMenu exporter turns into icon-data, which
            # is what the GNOME appindicators extension renders as a preview.
            item = Gtk.ImageMenuItem(label=label)
            item.set_always_show_image(True)
            image = Gtk.Image()
            image.set_size_request(PREVIEW_WIDTH, PREVIEW_HEIGHT)
            item.set_image(image)
            item.connect('activate', self._on_mode, mode)
            self.menu.append(item)
            self.items[mode] = {'item': item, 'image': image, 'base': label}

        strength_item = Gtk.MenuItem(label='Сила украшательств')
        strength_menu = Gtk.Menu()
        group = None
        for label, value in [('Лёгкая', 0.4), ('Средняя', 0.7),
                             ('Сильная', 1.0)]:
            if group is None:
                radio = Gtk.RadioMenuItem(label=label)
                group = radio
            else:
                radio = Gtk.RadioMenuItem.new_with_label_from_widget(
                    group, label)
            radio.connect('activate', self._on_strength, value)
            strength_menu.append(radio)
            self.strength_items[value] = radio
        strength_item.set_submenu(strength_menu)
        self.menu.append(strength_item)
        strength_menu.show_all()

        self.menu.append(Gtk.SeparatorMenuItem())
        refresh = Gtk.MenuItem(label='Обновить превью')
        refresh.connect('activate', lambda _item: self._refresh_previews())
        self.menu.append(refresh)
        window = Gtk.MenuItem(label='Показать превью крупно')
        window.connect('activate', lambda _item: self._show_preview_window())
        self.menu.append(window)
        self.menu.show_all()

    def _on_menu_show(self, _menu):
        self._refresh_previews()

    def _show_preview_window(self):
        if self.window is not None:
            self.window.present()
            self._refresh_previews()
            return
        self.window = Gtk.Window(title='Эффекты камеры (NPU)')
        self.window.set_default_size(760, 560)
        self.window.set_border_width(12)
        grid = Gtk.Grid(column_spacing=12, row_spacing=12,
                        column_homogeneous=True)
        for index, (mode, label) in enumerate(MODES):
            box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
            image = Gtk.Image()
            image.set_size_request(320, 180)
            title = Gtk.Label(label=label, xalign=0)
            choose = Gtk.Button(label='Выбрать')
            choose.connect('clicked', lambda _button, m=mode: self._on_mode(None, m))
            box.pack_start(image, False, False, 0)
            box.pack_start(title, False, False, 0)
            box.pack_start(choose, False, False, 0)
            grid.attach(box, index % 2, index // 2, 1, 1)
            self.window_images[mode] = {'image': image, 'label': title,
                                        'base': label}
        refresh = Gtk.Button(label='Обновить превью')
        refresh.connect('clicked', lambda _button: self._refresh_previews())
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        outer.pack_start(grid, True, True, 0)
        outer.pack_start(refresh, False, False, 0)
        self.window.add(outer)
        self.window.connect('destroy', self._on_window_destroy)
        self.window.show_all()
        self._refresh_previews()

    def _on_window_destroy(self, _window):
        self.window = None
        self.window_images = {}

    def _poll(self):
        self.ticks += 1
        was_active = self.active
        try:
            result = self.proxy.call_sync(
                'GetState', None, Gio.DBusCallFlags.NONE, 1500, None)
            state = value(result.unpack()[0])
            active = bool(value(state.get('active', False)))
            mode = str(value(state.get('mode', 'off')))
            self.active = active
            self.mode = mode
            self._apply_mode(mode)
            smooth = float(value(state.get('smooth', 0.7)))
            self.smooth = smooth
            self._apply_strength(smooth)
            if active and not was_active:
                self.ticks = 0
                self._refresh_previews()
            if active and not self.shown:
                self.indicator.set_status(
                    AppIndicator.IndicatorStatus.ACTIVE)
                self.shown = True
            elif not active and self.shown:
                self.indicator.set_status(
                    AppIndicator.IndicatorStatus.PASSIVE)
                self.shown = False
        except GLib.Error:
            if self.shown:
                self.indicator.set_status(
                    AppIndicator.IndicatorStatus.PASSIVE)
                self.shown = False
        if self.active and self.ticks and self.ticks % 5 == 0:
            self._refresh_previews()
        return GLib.SOURCE_CONTINUE

    def _apply_mode(self, mode):
        for key, entry in self.items.items():
            entry['item'].set_label(
                ('✓ ' if key == mode else '') + entry['base'])
        for key, entry in self.window_images.items():
            entry['label'].set_text(
                ('✓ ' if key == mode else '') + entry['base'])

    def _apply_strength(self, smooth):
        levels = [0.4, 0.7, 1.0]
        nearest = min(levels, key=lambda level: abs(level - smooth))
        self._updating_strength = True
        for value, item in self.strength_items.items():
            item.set_active(abs(value - nearest) < 1e-3)
        self._updating_strength = False

    def _on_strength(self, _item, value):
        if self._updating_strength:
            return
        if not self.proxy or not self.proxy.get_name_owner():
            return
        try:
            self.proxy.call_sync(
                'SetSmoothStrength', GLib.Variant('(d)', (value,)),
                Gio.DBusCallFlags.NONE, 1500, None)
            self.smooth = value
            target = ('blur-beauty'
                      if self.mode in ('blur', 'blur-beauty') else 'beauty')
            self._on_mode(None, target)
        except GLib.Error as e:
            print(f'set smooth error: {e.message}', file=sys.stderr)

    def _refresh_previews(self):
        if not self.proxy or not self.proxy.get_name_owner():
            return
        try:
            result = self.proxy.call_sync(
                'RefreshPreviews', None, Gio.DBusCallFlags.NONE, 30000, None)
            paths = value(result.unpack()[0])
            self.preview_paths = dict(paths)
        except GLib.Error as e:
            print(f'preview error: {e.message}', file=sys.stderr)
            return
        for mode, entry in self.items.items():
            path = self.preview_paths.get(mode)
            if not path or not os.path.exists(path):
                continue
            try:
                pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(
                    path, PREVIEW_WIDTH, PREVIEW_HEIGHT, True)
                entry['image'].set_from_pixbuf(pixbuf)
            except GLib.Error as e:
                print(f'preview load {path}: {e.message}', file=sys.stderr)
        for mode, entry in self.window_images.items():
            path = self.preview_paths.get(mode)
            if not path or not os.path.exists(path):
                continue
            try:
                pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(
                    path, 320, 180, True)
                entry['image'].set_from_pixbuf(pixbuf)
            except GLib.Error as e:
                print(f'preview load {path}: {e.message}', file=sys.stderr)

    def _on_mode(self, _item, mode):
        if not self.proxy or not self.proxy.get_name_owner():
            return
        try:
            variant = GLib.Variant('(s)', (mode,))
            self.proxy.call_sync(
                'SetMode', variant, Gio.DBusCallFlags.NONE, 1500, None)
            self.mode = mode
            self._apply_mode(mode)
            self._refresh_previews()
        except GLib.Error as e:
            print(f'set mode error: {e.message}', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--show-preview', action='store_true',
                        help='open the large preview window on start')
    args = parser.parse_args()
    indicator = Indicator()
    if args.show_preview:
        GLib.timeout_add(800, lambda: (indicator._show_preview_window(),
                                       GLib.SOURCE_REMOVE)[1])
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    Gtk.main()


if __name__ == '__main__':
    main()
