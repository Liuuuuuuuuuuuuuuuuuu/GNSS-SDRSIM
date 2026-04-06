#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
if [ -d "$SCRIPT_DIR/@BIN_DIR@" ]; then
  ROOT_DIR="$SCRIPT_DIR"
elif [ -d "$SCRIPT_DIR/../@BIN_DIR@" ]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  ROOT_DIR="$SCRIPT_DIR"
fi
BIN_DIR="@BIN_DIR@"
REBUILD_SCRIPT="@REBUILD_SCRIPT@"
FAT_BIN="$ROOT_DIR/$BIN_DIR/bds-sim-fat"

PORTABLE_LIB_DIRS=""
if [ -d "$ROOT_DIR/lib" ]; then
  PORTABLE_LIB_DIRS="$ROOT_DIR/lib"
fi
if [ -d "$ROOT_DIR/lib64" ]; then
  PORTABLE_LIB_DIRS="${PORTABLE_LIB_DIRS:+$PORTABLE_LIB_DIRS:}$ROOT_DIR/lib64"
fi
if [ -n "$PORTABLE_LIB_DIRS" ]; then
  export LD_LIBRARY_PATH="$PORTABLE_LIB_DIRS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

print_rebuild_hint() {
  if [ -x "$ROOT_DIR/$REBUILD_SCRIPT" ]; then
    echo "[launcher] Quick rebuild: $ROOT_DIR/$REBUILD_SCRIPT" >&2
  else
    echo "[launcher] Rebuild with: make clean && make" >&2
  fi
}

if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "[launcher] nvidia-smi not found. NVIDIA driver/runtime may be missing." >&2
fi

gpu_cc=""
if command -v nvidia-smi >/dev/null 2>&1; then
  gpu_cc=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n 1 | tr -d "[:space:]" || true)
fi

if [ -x "$FAT_BIN" ]; then
  if ldd "$FAT_BIN" 2>/dev/null | grep -q "not found"; then
    echo "[launcher] Missing runtime libraries for $FAT_BIN:" >&2
    ldd "$FAT_BIN" 2>/dev/null | grep "not found" >&2 || true
    print_rebuild_hint
    exit 2
  fi
  echo "[launcher] Using bds-sim-fat (compute_cap=${gpu_cc:-unknown})" >&2
  exec "$FAT_BIN" "$@"
fi

for bin in bds-sim-modern bds-sim-ada bds-sim-ampere bds-sim-turing bds-sim-blackwell bds-sim-pascal; do
  if [ -x "$ROOT_DIR/$BIN_DIR/$bin" ]; then
    echo "[launcher] Using fallback $bin (compute_cap=${gpu_cc:-unknown})" >&2
    exec "$ROOT_DIR/$BIN_DIR/$bin" "$@"
  fi
done

echo "[launcher] No runnable bds-sim binary found under $ROOT_DIR/$BIN_DIR." >&2
print_rebuild_hint
exit 2
