#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

PKG="npu-camfx"
VERSION="${VERSION:-0.1.0-1}"
ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
BUILD_DIR="$PWD/build/deb-build"
OUT_DIR="$PWD/build/deb"
ROOT="$OUT_DIR/${PKG}_${VERSION}_${ARCH}"
DOC="$ROOT/usr/share/doc/$PKG"
OV_DEST="$ROOT/usr/lib/npu-camfx/openvino"

OPENVINO_DIR="$("$PWD/scripts/openvino-dir.sh")"
if [[ ! -d "$OPENVINO_DIR" ]]; then
    echo "OpenVINO CMake package not found: $OPENVINO_DIR" >&2
    exit 1
fi
OV_PACKAGE_DIR="$(dirname "$OPENVINO_DIR")"
OV_LIBS="$OV_PACKAGE_DIR/libs"
SITE_PACKAGES="$(dirname "$OV_PACKAGE_DIR")"
OV_DIST_INFO="$(find "$SITE_PACKAGES" -maxdepth 1 -type d -name 'openvino-*.dist-info' | head -1)"

if [[ ! -d "$OV_LIBS" ]]; then
    echo "OpenVINO runtime libraries not found: $OV_LIBS" >&2
    exit 1
fi

MODELS_SRC="${NPUCAM_MODELS:-$HOME/npu-models}"
MODEL_FILES=(
    selfie_mp_fp16.onnx
    face-detection-retail-0004.xml
    face-detection-retail-0004.bin
    facial-landmarks-35-adas-0002.xml
    facial-landmarks-35-adas-0002.bin
)
for model in "${MODEL_FILES[@]}"; do
    if [[ ! -f "$MODELS_SRC/$model" ]]; then
        MODELS_SRC="$PWD/build/deb/models"
        echo "Downloading model files into $MODELS_SRC"
        ./scripts/download-models.sh "$MODELS_SRC"
        break
    fi
done
for model in "${MODEL_FILES[@]}"; do
    if [[ ! -f "$MODELS_SRC/$model" ]]; then
        echo "Missing model: $MODELS_SRC/$model" >&2
        exit 1
    fi
done

rm -rf "$BUILD_DIR" "$ROOT"
mkdir -p "$OUT_DIR"

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DOpenVINO_DIR="$OPENVINO_DIR" \
    -DCMAKE_INSTALL_RPATH=/usr/lib/npu-camfx/openvino \
    -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"

for binary in npueffectsd npucam-preview libgstnpueffects.so; do
    if readelf -d "$BUILD_DIR/$binary" | grep -q '/home/'; then
        echo "Refusing to package a binary with a home-directory RUNPATH: $binary" >&2
        exit 1
    fi
done

install -Dm755 "$BUILD_DIR/npueffectsd" "$ROOT/usr/bin/npueffectsd"
install -Dm755 "$BUILD_DIR/npucam-preview" "$ROOT/usr/bin/npucam-preview"
install -Dm755 ui/npu-effects-indicator.py "$ROOT/usr/bin/npu-effects-indicator"

install -Dm644 "$BUILD_DIR/libgstnpueffects.so" \
    "$ROOT/usr/lib/$MULTIARCH/gstreamer-1.0/libgstnpueffects.so"

install -Dm644 packaging/systemd/npu-effects.service \
    "$ROOT/usr/lib/systemd/user/npu-effects.service"
install -Dm644 packaging/systemd/npu-effects-ui.service \
    "$ROOT/usr/lib/systemd/user/npu-effects-ui.service"

# Enable the user units for every graphical session without touching
# per-user configuration. systemd reads /etc/systemd/user for all users.
WANTS="$ROOT/etc/systemd/user/graphical-session.target.wants"
mkdir -p "$WANTS"
ln -s ../../../usr/lib/systemd/user/npu-effects.service \
    "$WANTS/npu-effects.service"
ln -s ../../../usr/lib/systemd/user/npu-effects-ui.service \
    "$WANTS/npu-effects-ui.service"

install -Dm644 packaging/wireplumber/60-npu-camfx-hide-libcamera.conf \
    "$ROOT/etc/wireplumber/wireplumber.conf.d/60-npu-camfx-hide-libcamera.conf"

install -Dm755 scripts/migrate-user-install.sh \
    "$ROOT/usr/share/npu-camfx/migrate-user-install.sh"

mkdir -p "$ROOT/usr/share/npu-camfx/models" "$OV_DEST" "$DOC"
for model in "${MODEL_FILES[@]}"; do
    install -m644 "$MODELS_SRC/$model" "$ROOT/usr/share/npu-camfx/models/$model"
done

# A self-contained OpenVINO runtime; no Python virtual environment is
# required at run time.
OV_PATTERNS=(
    "libopenvino.so.*"
    "libopenvino_c.so.*"
    "libopenvino_ir_frontend.so.*"
    "libopenvino_onnx_frontend.so.*"
    "libopenvino_intel_cpu_plugin.so"
    "libopenvino_intel_npu_plugin.so"
    "libopenvino_auto_plugin.so"
    "libopenvino_hetero_plugin.so"
    "libtbb.so.*"
    "libtbbbind_*.so.*"
    "libtbbmalloc.so.*"
    "libtbbmalloc_proxy.so.*"
    "libhwloc.so.*"
)
copied=0
for pattern in "${OV_PATTERNS[@]}"; do
    for file in "$OV_LIBS"/$pattern; do
        [[ -e "$file" ]] || continue
        install -m644 "$file" "$OV_DEST/$(basename "$file")"
        copied=$((copied + 1))
    done
done
if [[ "$copied" -eq 0 ]]; then
    echo "No OpenVINO runtime files matched in $OV_LIBS" >&2
    exit 1
fi

install -m644 README.md "$DOC/README.md"
install -m644 LICENSE "$DOC/LICENSE"
install -m644 packaging/README.Debian "$DOC/README.Debian"
install -m644 packaging/copyright "$DOC/copyright"
install -m644 packaging/MODELS.md "$DOC/MODELS.md"
gzip -9n -c CHANGELOG.md > "$DOC/changelog.gz"
chmod 644 "$DOC/changelog.gz"
cp -a docs "$DOC/docs"
find "$DOC/docs/evidence" -maxdepth 1 -type f \
    \( -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.png' -o -iname '*.webp' \) \
    -delete 2>/dev/null || true

if [[ -n "$OV_DIST_INFO" && -f "$OV_DIST_INFO/licenses/LICENSE" ]]; then
    install -m644 "$OV_DIST_INFO/licenses/LICENSE" "$DOC/OPENVINO-LICENSE"
fi
if [[ -f /usr/share/common-licenses/Apache-2.0 ]]; then
    install -m644 /usr/share/common-licenses/Apache-2.0 "$DOC/LICENSE-APACHE-2.0"
fi
install -Dm644 packaging/v4l2loopback/60-npu-camfx.conf \
    "$DOC/examples/60-npu-camfx.conf"

mkdir -p "$ROOT/DEBIAN"
install -m755 packaging/postinst "$ROOT/DEBIAN/postinst"
install -m755 packaging/postrm "$ROOT/DEBIAN/postrm"
printf '%s\n' "/etc/wireplumber/wireplumber.conf.d/60-npu-camfx-hide-libcamera.conf" \
    > "$ROOT/DEBIAN/conffiles"

INSTALLED_SIZE="$(du -sk "$ROOT/usr" | cut -f1)"
DEPENDS="libc6, libgcc-s1, libstdc++6, libglib2.0-0t64, libgstreamer1.0-0, libgstreamer-plugins-base1.0-0, libopencv-core410, libopencv-imgproc410, libopencv-imgcodecs410, libze1, libgomp1, libnuma1, libtbb12, python3, python3-gi, gir1.2-gtk-3.0, gir1.2-ayatanaappindicator3-0.1, libayatana-appindicator3-1, gstreamer1.0-libcamera, gstreamer1.0-plugins-base, gstreamer1.0-plugins-good, systemd"
RECOMMENDS="wireplumber, gnome-shell-ubuntu-extensions"
SUGGESTS="intel-level-zero-npu, intel-driver-compiler-npu, intel-fw-npu, v4l2loopback-dkms"

cat > "$ROOT/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: Yegor Chuperka <9316951+fstronin@users.noreply.github.com>
Installed-Size: $INSTALLED_SIZE
Depends: $DEPENDS
Recommends: $RECOMMENDS
Suggests: $SUGGESTS
Section: video
Priority: optional
Homepage: https://github.com/fstronin/npu-camfx
Description: NPU-accelerated virtual camera effects for Linux
 NpuCamFX provides background blur, skin smoothing, and luma gamma
 correction through a V4L2 virtual camera. It runs the GStreamer
 npueffects element on the Intel NPU through OpenVINO and ships a
 GNOME StatusNotifierItem control and systemd user units.
 .
 The package bundles the OpenVINO runtime and the required model files
 under /usr/lib/npu-camfx and /usr/share/npu-camfx. It does not write
 application files into user home directories.
EOF

(
    cd "$ROOT"
    find . -type d -exec chmod 755 {} +
    find . -type f -exec chmod 644 {} +
    chmod 755 ./usr/bin/* ./usr/share/npu-camfx/migrate-user-install.sh \
        ./DEBIAN/postinst ./DEBIAN/postrm
    find . -path ./DEBIAN -prune -o -type f -print \
        | sed 's|^\./||' \
        | sort \
        | xargs -r md5sum > DEBIAN/md5sums
)

DEB="$OUT_DIR/${PKG}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$ROOT" "$DEB"

if dpkg-deb -c "$DEB" | grep -qE ' (/(home|root)/|/home/)'; then
    echo "Refusing to keep user-directory paths in the package" >&2
    exit 1
fi

echo
echo "Built: $DEB"
echo
dpkg-deb --info "$DEB"
echo
echo "Install with:"
echo "  sudo apt install \"$DEB\""
echo "Then, as the desktop user:"
echo "  systemctl --user daemon-reload"
echo "  systemctl --user start npu-effects.service npu-effects-ui.service"
