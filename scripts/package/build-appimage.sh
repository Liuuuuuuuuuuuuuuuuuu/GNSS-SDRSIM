#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/../.." && pwd)"
APP_NAME="gnss-sdrsim"
APPDIR="$ROOT_DIR/build/package/appimage/AppDir"
TOOLS_DIR="$ROOT_DIR/build/package/tools"
LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"

mkdir -p "$TOOLS_DIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/lib/gnss-sdrsim" "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps"

if [[ ! -x "$LINUXDEPLOY" ]]; then
  curl -L "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" -o "$LINUXDEPLOY"
  chmod +x "$LINUXDEPLOY"
fi
PLUGIN_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
if [[ ! -x "$PLUGIN_QT" ]]; then
  curl -L "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" -o "$PLUGIN_QT"
  chmod +x "$PLUGIN_QT"
fi

for path in gnss-sim bin scripts assets data fonts BRDM ne_50m_land runtime_paths; do
  if [[ -e "$ROOT_DIR/$path" ]]; then
    cp -a "$ROOT_DIR/$path" "$APPDIR/usr/lib/gnss-sdrsim/"
  fi
done

cat > "$APPDIR/usr/bin/gnss-sim" <<'EOF'
#!/usr/bin/env bash
set -e
SELF_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
exec "$SELF_DIR/../lib/gnss-sdrsim/gnss-sim" "$@"
EOF
chmod +x "$APPDIR/usr/bin/gnss-sim"

install -m 0644 "$ROOT_DIR/packaging/linux/gnss-sim.desktop" "$APPDIR/usr/share/applications/gnss-sim.desktop"
install -m 0644 "$ROOT_DIR/packaging/linux/gnss-sim.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/gnss-sim.svg"

cat > "$APPDIR/AppRun" <<'EOF'
#!/usr/bin/env bash
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
exec "$HERE/usr/bin/gnss-sim" "$@"
EOF
chmod +x "$APPDIR/AppRun"

OUT_DIR="$ROOT_DIR/build/package/appimage"
mkdir -p "$OUT_DIR"
export QML_SOURCES_PATHS="$ROOT_DIR/gui"
FINAL_APPIMAGE="$OUT_DIR/${APP_NAME}-$(date +%Y.%m.%d)-x86_64.AppImage"
rm -f "$FINAL_APPIMAGE"
export LDAI_OUTPUT="$FINAL_APPIMAGE"
"$LINUXDEPLOY" --appdir "$APPDIR" --desktop-file "$APPDIR/usr/share/applications/gnss-sim.desktop" --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/gnss-sim.svg" --plugin qt --output appimage

if [[ -f "$FINAL_APPIMAGE" ]]; then
  echo "[package-appimage] created: $FINAL_APPIMAGE"
else
  echo "[package-appimage] failed: expected output not found at $FINAL_APPIMAGE" >&2
  exit 1
fi
