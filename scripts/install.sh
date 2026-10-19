#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

PREFIX="${PREFIX:-$HOME/.local}"

daemon_was_active="$(systemctl --user is-active npu-effects.service 2>/dev/null || true)"
ui_was_active="$(systemctl --user is-active npu-effects-ui.service 2>/dev/null || true)"
if [[ "$daemon_was_active" == "active" || "$ui_was_active" == "active" ]]; then
    systemctl --user stop npu-effects-ui.service npu-effects.service \
        2>/dev/null || true
fi

"$PWD/scripts/build.sh"
cmake --install build
# CMake may skip a same-timestamp plugin; force the current build.
install -Dm644 build/libgstnpueffects.so \
    "$PREFIX/share/gstreamer-1.0/plugins/libgstnpueffects.so"

install -Dm755 ui/npu-effects-indicator.py "$PREFIX/bin/npu-effects-indicator"
install -Dm644 systemd/npu-effects.service \
    "$HOME/.config/systemd/user/npu-effects.service"
install -Dm644 systemd/npu-effects-ui.service \
    "$HOME/.config/systemd/user/npu-effects-ui.service"

if [[ -f ui/vendor/AyatanaAppIndicator3-0.1.typelib ]]; then
    install -Dm644 ui/vendor/AyatanaAppIndicator3-0.1.typelib \
        "$PREFIX/lib/npu-camera-effects/girepository-1.0/AyatanaAppIndicator3-0.1.typelib"
elif ! python3 -c 'import gi; gi.require_version("AyatanaAppIndicator3", "0.1")' \
        2>/dev/null; then
    echo "warning: AyatanaAppIndicator3 typelib not found." >&2
    echo "Install gir1.2-ayatanaappindicator3-0.1 before starting the UI." >&2
fi

systemctl --user daemon-reload

if [[ "$daemon_was_active" == "active" ]]; then
    systemctl --user start npu-effects.service
fi
if [[ "$ui_was_active" == "active" ]]; then
    systemctl --user start npu-effects-ui.service
fi

echo "installed to $PREFIX"
echo "models are expected in ${NPUCAM_MODELS:-$HOME/npu-models}"
echo "enable the services with:"
echo "  systemctl --user enable --now npu-effects.service npu-effects-ui.service"
