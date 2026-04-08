#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"

extract_nvcc_version() {
  local nvcc_bin="$1"
  "$nvcc_bin" --version 2>/dev/null | sed -n 's/.*release \([0-9][0-9.]*\).*/\1/p' | head -n1
}

extract_driver_cuda_version() {
  if ! command -v nvidia-smi >/dev/null 2>&1; then
    return 0
  fi
  nvidia-smi 2>/dev/null | sed -n 's/.*CUDA Version: \([0-9][0-9.]*\).*/\1/p' | head -n1
}

version_le() {
  local a="$1"
  local b="$2"
  [ "$(printf '%s\n%s\n' "$a" "$b" | sort -V | head -n1)" = "$a" ]
}

pick_nvcc() {
  local forced_nvcc="${NVCC:-}"
  if [ -n "$forced_nvcc" ]; then
    echo "$forced_nvcc"
    return 0
  fi

  local driver_cuda
  driver_cuda="$(extract_driver_cuda_version || true)"
  local candidates=()
  local p

  if [ -x /usr/local/cuda/bin/nvcc ]; then
    candidates+=(/usr/local/cuda/bin/nvcc)
  fi
  for p in /usr/local/cuda-*/bin/nvcc; do
    [ -x "$p" ] && candidates+=("$p")
  done
  if command -v nvcc >/dev/null 2>&1; then
    candidates+=("$(command -v nvcc)")
  fi

  if [ ${#candidates[@]} -eq 0 ]; then
    echo ""
    return 0
  fi

  local uniq=()
  local seen=""
  for p in "${candidates[@]}"; do
    case ":$seen:" in
      *":$p:"*) ;;
      *)
        uniq+=("$p")
        seen="$seen:$p"
        ;;
    esac
  done

  local best_nvcc=""
  local best_ver=""
  local ver

  for p in "${uniq[@]}"; do
    ver="$(extract_nvcc_version "$p")"
    [ -n "$ver" ] || continue

    if [ -n "$driver_cuda" ] && ! version_le "$ver" "$driver_cuda"; then
      continue
    fi

    if [ -z "$best_ver" ] || [ "$(printf '%s\n%s\n' "$best_ver" "$ver" | sort -V | tail -n1)" = "$ver" ]; then
      best_ver="$ver"
      best_nvcc="$p"
    fi
  done

  if [ -n "$best_nvcc" ]; then
    echo "$best_nvcc"
    return 0
  fi

  echo "${uniq[0]}"
}

NVCC_BIN="$(pick_nvcc)"

if [ -z "$NVCC_BIN" ]; then
  echo "[rebuild] nvcc not found in PATH. Install CUDA toolkit first." >&2
  exit 2
fi

CUDA_ROOT="$(cd "$(dirname "$NVCC_BIN")/.." && pwd)"
NVCC_VER="$(extract_nvcc_version "$NVCC_BIN")"
DRIVER_CUDA_VER="$(extract_driver_cuda_version || true)"

echo "[rebuild] ROOT=$ROOT_DIR"
echo "[rebuild] NVCC=$NVCC_BIN"
echo "[rebuild] NVCC_VERSION=${NVCC_VER:-unknown}"
echo "[rebuild] CUDA_ROOT=$CUDA_ROOT"
echo "[rebuild] DRIVER_CUDA_MAX=${DRIVER_CUDA_VER:-unknown}"
cd "$ROOT_DIR"
make clean
NVCC="$NVCC_BIN" CUDA_PATH="$CUDA_ROOT" make -j"$(nproc)"
