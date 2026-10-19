# Evidence

The public repository intentionally does **not** include camera stills or
screenshots from the development machine: they contain a person's face and
desktop. The following files are therefore local-only and are ignored by
Git:

| Local file | What it shows |
|---|---|
| `live-off.jpg` | `off` mode (gamma 0.85) |
| `live-blur.jpg` | `blur`: background blurred, person sharp |
| `live-beauty.jpg` | `beauty` (0.7): forehead and cheeks smoothed |
| `live-blur-beauty.jpg` | both effects |
| `topbar-active.png` | camera icon in the top bar while the camera streams |
| `menu-preview.png` | 160x90 D-Bus menu previews |
| `preview-window.png` | 2x2 large-preview window |
| `beauty-before-after.png` | controlled CLI comparison of `off` and `beauty` |

To generate local evidence, use the virtual camera directly:

```bash
gst-launch-1.0 -q v4l2src device=/dev/video60 num-buffers=10 \
  ! videoconvert ! jpegenc ! multifilesink location=/tmp/frame-%02d.jpg
```

The 256x256 segmentation mask has soft edges around hair and shoulders;
some false-positive areas (for example, a chair) are expected. The mask is
stabilized with a 3x3 median filter and a motion-gated EMA.
