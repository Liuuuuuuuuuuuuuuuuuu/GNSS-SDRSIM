#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
NVCC_BIN="${NVCC:-$(command -v nvcc 2>/dev/null || true)}"

if [ -z "$NVCC_BIN" ]; then
  echo "[rebuild] nvcc not found in PATH. Install CUDA toolkit first." >&2
  exit 2
fi

echo "[rebuild] ROOT=$ROOT_DIR"
echo "[rebuild] NVCC=$NVCC_BIN"
cd "$ROOT_DIR"
make clean
NVCC="$NVCC_BIN" make -j"$(nproc)"
