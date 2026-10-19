#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
    echo "Run this helper as your desktop user, not with sudo." >&2
    exit 1
fi

if [[ ! -x /usr/bin/npueffectsd ]]; then
    echo "The NpuCamFX system package is not installed." >&2
    echo "Install it first: sudo apt install ./npu-camfx_*.deb" >&2
    exit 1
fi

systemctl --user disable --now npu-effects.service npu-effects-ui.service \
    2>/dev/null || true

# Old user-level units take precedence over the packaged system units.
rm -f "$HOME/.config/systemd/user/npu-effects.service" \
      "$HOME/.config/systemd/user/npu-effects-ui.service"
rm -rf "$HOME/.config/systemd/user/npu-effects.service.d" \
       "$HOME/.config/systemd/user/npu-effects-ui.service.d"
rm -f "$HOME/.config/systemd/user/graphical-session.target.wants/npu-effects.service" \
      "$HOME/.config/systemd/user/graphical-session.target.wants/npu-effects-ui.service"

# Old per-user binaries, GStreamer plugin, and vendored typelib.
rm -f "$HOME/.local/bin/npueffectsd" \
      "$HOME/.local/bin/npucam-preview" \
      "$HOME/.local/bin/npu-effects-indicator"
rm -f "$HOME/.local/share/gstreamer-1.0/plugins/libgstnpueffects.so"
rm -rf "$HOME/.local/lib/npu-camera-effects"

# The packaged WirePlumber rule replaces the old user-level workaround.
rm -f "$HOME/.config/wireplumber/wireplumber.conf.d/60-hide-libcamera.conf"

systemctl --user daemon-reload
systemctl --user restart wireplumber 2>/dev/null || true
systemctl --user start npu-effects.service npu-effects-ui.service

echo "NpuCamFX now runs from /usr/bin and /usr/lib/npu-camfx."
echo "The development OpenVINO environment and model cache were left intact:"
echo "  $HOME/npu-venv"
echo "  $HOME/npu-models"
echo
systemctl --user --no-pager --lines=4 status \
    npu-effects.service npu-effects-ui.service || true
