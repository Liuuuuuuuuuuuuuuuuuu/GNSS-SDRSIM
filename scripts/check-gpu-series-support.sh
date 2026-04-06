#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)"
cd "$ROOT_DIR"

nvcc_bin="${NVCC:-$(command -v nvcc 2>/dev/null || true)}"
nvcc_codes=""
if [ -n "$nvcc_bin" ]; then
  nvcc_codes="$($nvcc_bin --list-gpu-code 2>/dev/null || true)"
fi

gencode_fat="$(make -s print-gencode-fat 2>/dev/null || true)"

jit_disabled=0
if [ "${CUDA_DISABLE_JIT:-0}" = "1" ] || [ "${CUDA_DISABLE_PTX_JIT:-0}" = "1" ] || [ "${BDS_CUDA_DISABLE_JIT:-0}" = "1" ]; then
  jit_disabled=1
fi

has_bin() {
  [ -x "$ROOT_DIR/bin/$1" ]
}

has_sm_in_fat() {
  local sm="$1"
  printf '%s' "$gencode_fat" | grep -q "code=sm_${sm}"
}

ptx_floor_from_fat() {
  local ptx
  ptx="$(printf '%s' "$gencode_fat" | grep -o 'code=compute_[0-9][0-9]*' | sed 's/code=compute_//' | head -n 1 || true)"
  printf '%s' "$ptx"
}

classify_series() {
  local series="$1" sm_need="$2" bin_name="$3"
  local ptx_floor status reason
  ptx_floor="$(ptx_floor_from_fat)"

  if has_bin "$bin_name" || has_sm_in_fat "$sm_need"; then
    status="direct"
    reason="has native SASS (sm_${sm_need})"
  elif has_bin "bds-sim-fat" && [ -n "$ptx_floor" ] && [ "$jit_disabled" -eq 0 ] && [ "$sm_need" -ge "$ptx_floor" ]; then
    status="fallback"
    reason="PTX JIT fallback from compute_${ptx_floor}"
  elif has_bin "bds-sim-fat" && [ -n "$ptx_floor" ] && [ "$jit_disabled" -eq 1 ]; then
    status="unsupported"
    reason="PTX exists but JIT is disabled"
  else
    status="unsupported"
    reason="no matching SASS/PTX fallback"
  fi

  printf '%-8s %-12s %s\n' "$series" "$status" "$reason"
}

echo "[check] root: $ROOT_DIR"
if [ -n "$nvcc_bin" ]; then
  echo "[check] nvcc: $nvcc_bin"
  echo "[check] nvcc gpu codes: ${nvcc_codes:-<none>}"
else
  echo "[check] nvcc: <not found>"
fi

echo "[check] built bins:"
ls -1 "$ROOT_DIR/bin" 2>/dev/null | sed 's/^/  - /' || echo "  - <none>"

echo "[check] fat gencode: ${gencode_fat:-<unknown>}"
if [ "$jit_disabled" -eq 1 ]; then
  echo "[check] JIT policy: disabled (CUDA_DISABLE_JIT/CUDA_DISABLE_PTX_JIT/BDS_CUDA_DISABLE_JIT)"
else
  echo "[check] JIT policy: enabled"
fi

echo
echo "Series   Status       Note"
echo "-----------------------------------------------"
classify_series "GTX10" 61 "bds-sim-pascal"
classify_series "RTX20" 75 "bds-sim-turing"
classify_series "RTX30" 86 "bds-sim-ampere"
classify_series "RTX40" 89 "bds-sim-ada"
# Treat RTX50 as requiring Blackwell-class code path; sm_120 as baseline for direct path.
classify_series "RTX50" 120 "bds-sim-blackwell"
