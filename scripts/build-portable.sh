#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)"
PORTABLE_NAME="${PORTABLE_NAME:-GNSS-SDRSIM}"
PORTABLE_VERSION="${PORTABLE_VERSION:-1.0}"
OUTPUT_DIR="${PORTABLE_RELEASE_DIR:-${1:-$ROOT_DIR/dist/${PORTABLE_NAME}-${PORTABLE_VERSION}-portable}}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

ensure_built() {
  if [ ! -x "$ROOT_DIR/bds-sim" ] || [ ! -x "$ROOT_DIR/bin/bds-sim-fat" ]; then
    echo "[portable] build outputs are missing; running make -j${MAKE_JOBS}" >&2
    (cd "$ROOT_DIR" && make -j"$MAKE_JOBS")
  fi
}

copy_tree() {
  src="$1"
  dst="$2"
  if [ -d "$src" ]; then
    mkdir -p "$dst"
    cp -a "$src/." "$dst/"
  fi
}

copy_file_if_exists() {
  src="$1"
  dst="$2"
  if [ -e "$src" ]; then
    mkdir -p "$(dirname "$dst")"
    cp -a "$src" "$dst"
  fi
}

bundle_libs_from_binary() {
  binary_path="$1"
  if [ ! -x "$binary_path" ]; then
    return 0
  fi

  while read -r line; do
    case "$line" in
      *"=> not found")
        echo "[portable] missing dependency for $binary_path: $line" >&2
        exit 2
        ;;
      *"=>"*"/"*)
        lib_path=$(printf '%s\n' "$line" | awk '{ for (i = 1; i <= NF; i++) { if ($i ~ /^\//) { print $i; exit } } }')
        if [ -n "$lib_path" ] && [ -e "$lib_path" ]; then
          case "$lib_path" in
            /lib64/ld-linux-*|/lib/x86_64-linux-gnu/ld-linux-*|/lib/ld-linux-*)
              continue
              ;;
          esac
          dest_path="$OUTPUT_DIR/lib/$(basename "$lib_path")"
          if [ ! -e "$dest_path" ]; then
            cp -L "$lib_path" "$dest_path"
          fi
        fi
        ;;
      /*)
        lib_path=$(printf '%s\n' "$line" | awk '{ print $1 }')
        if [ -n "$lib_path" ] && [ -e "$lib_path" ]; then
          case "$lib_path" in
            /lib64/ld-linux-*|/lib/x86_64-linux-gnu/ld-linux-*|/lib/ld-linux-*)
              continue
              ;;
          esac
          dest_path="$OUTPUT_DIR/lib/$(basename "$lib_path")"
          if [ ! -e "$dest_path" ]; then
            cp -L "$lib_path" "$dest_path"
          fi
        fi
        ;;
    esac
  done < <(ldd "$binary_path" 2>/dev/null)
}

ensure_built

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR/bin" "$OUTPUT_DIR/lib" "$OUTPUT_DIR/scripts"

copy_file_if_exists "$ROOT_DIR/bds-sim" "$OUTPUT_DIR/bds-sim"
copy_file_if_exists "$ROOT_DIR/bin/bds-sim-fat" "$OUTPUT_DIR/bin/bds-sim-fat"

for bin_name in bds-sim-modern bds-sim-ada bds-sim-ampere bds-sim-turing bds-sim-blackwell bds-sim-pascal; do
  copy_file_if_exists "$ROOT_DIR/bin/$bin_name" "$OUTPUT_DIR/bin/$bin_name"
done

copy_tree "$ROOT_DIR/BRDM" "$OUTPUT_DIR/BRDM"
copy_tree "$ROOT_DIR/fonts" "$OUTPUT_DIR/fonts"
copy_tree "$ROOT_DIR/ne_50m_land" "$OUTPUT_DIR/ne_50m_land"

copy_file_if_exists "$ROOT_DIR/scripts/rebuild-local.sh" "$OUTPUT_DIR/scripts/rebuild-local.sh"
copy_file_if_exists "$ROOT_DIR/scripts/check-gpu-series-support.sh" "$OUTPUT_DIR/scripts/check-gpu-series-support.sh"
copy_file_if_exists "$ROOT_DIR/scripts/portable-self-check.sh" "$OUTPUT_DIR/scripts/portable-self-check.sh"
copy_file_if_exists "$ROOT_DIR/scripts/portable-oneclick-install.sh" "$OUTPUT_DIR/scripts/portable-oneclick-install.sh"

for script_path in \
  "$OUTPUT_DIR/scripts/rebuild-local.sh" \
  "$OUTPUT_DIR/scripts/check-gpu-series-support.sh" \
  "$OUTPUT_DIR/scripts/portable-self-check.sh" \
  "$OUTPUT_DIR/scripts/portable-oneclick-install.sh"; do
  [ -e "$script_path" ] || continue
  chmod +x "$script_path"
done

for executable in "$OUTPUT_DIR/bds-sim" "$OUTPUT_DIR/bin/"*; do
  [ -x "$executable" ] || continue
  bundle_libs_from_binary "$executable"
done

cat > "$OUTPUT_DIR/README.txt" <<'EOF'
GNSS-SDRSIM portable package

Run from this directory:
  ./bds-sim

The package keeps data folders beside the launcher so relative paths continue to work.
EOF

cat > "$OUTPUT_DIR/VERSION.txt" <<EOF
name=$PORTABLE_NAME
version=$PORTABLE_VERSION
release_dir=$OUTPUT_DIR
EOF

echo "[portable] staged package at: $OUTPUT_DIR"