#!/usr/bin/env bash
set -euo pipefail

systemctl --user disable --now npu-effects.service npu-effects-ui.service \
    2>/dev/null || true
systemctl --user unmask gc2607-vcam.service 2>/dev/null || true
systemctl --user enable --now gc2607-vcam.service cam-preset.service
sleep 1
systemctl --user --no-pager --lines=3 status gc2607-vcam.service cam-preset.service || true
