#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/../.." && pwd)"
PKG_NAME="gnss-sdrsim"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
VERSION="${1:-}"

if [[ -z "$VERSION" ]]; then
  GIT_TAG="$(git -C "$ROOT_DIR" describe --tags --always --dirty 2>/dev/null || true)"
  DATE_TAG="$(date +%Y.%m.%d)"
  VERSION="${DATE_TAG}-${GIT_TAG:-local}"
  VERSION="${VERSION//\//-}"
fi

STAGE_BASE="$ROOT_DIR/build/package/deb"
STAGE_DIR="$STAGE_BASE/${PKG_NAME}_${VERSION}_${ARCH}"
INSTALL_ROOT="$STAGE_DIR/usr/lib/gnss-sdrsim"
mkdir -p "$STAGE_DIR/DEBIAN" "$INSTALL_ROOT" "$STAGE_DIR/usr/bin" "$STAGE_DIR/usr/share/applications"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/DEBIAN" "$INSTALL_ROOT" "$STAGE_DIR/usr/bin" "$STAGE_DIR/usr/share/applications"

for path in gnss-sim bin scripts assets data fonts BRDM ne_50m_land runtime_paths; do
  if [[ -e "$ROOT_DIR/$path" ]]; then
    cp -a "$ROOT_DIR/$path" "$INSTALL_ROOT/"
  fi
done

cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: science
Priority: optional
Architecture: ${ARCH}
Maintainer: GNSS-SDRSIM Packager <packager@local>
Depends: libc6, libstdc++6, libgcc-s1
Description: GNSS-SDRSIM launcher package
 Provides GNSS/GPS launcher entries and runtime assets.
EOF

ln -sf ../lib/gnss-sdrsim/gnss-sim "$STAGE_DIR/usr/bin/gnss-sim"

install -m 0644 "$ROOT_DIR/packaging/linux/gnss-sim.desktop" "$STAGE_DIR/usr/share/applications/gnss-sim.desktop"

OUT_DEB="$STAGE_BASE/${PKG_NAME}_${VERSION}_${ARCH}.deb"
rm -f "$OUT_DEB"
dpkg-deb --build "$STAGE_DIR" "$OUT_DEB"
echo "[package-deb] created: $OUT_DEB"
