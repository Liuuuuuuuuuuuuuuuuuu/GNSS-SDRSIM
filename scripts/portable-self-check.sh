#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)"
BIN_DIR="bin"
PROGRESS_TOTAL=21
PROGRESS_WIDTH=30
progress_done=0
ok_count=0
warn_count=0
fail_count=0
prompt_allowed=1

have() {
  command -v "$1" >/dev/null 2>&1
}

draw_progress() {
  filled=$((progress_done * PROGRESS_WIDTH / PROGRESS_TOTAL))
  empty=$((PROGRESS_WIDTH - filled))
  percent=$((progress_done * 100 / PROGRESS_TOTAL))

  bar_filled="$(printf '%*s' "$filled" '' | tr ' ' '#')"
  bar_empty="$(printf '%*s' "$empty" '' | tr ' ' '-')"
  printf '\r[progress] [%s%s] %3d%% (%d/%d)' "$bar_filled" "$bar_empty" "$percent" "$progress_done" "$PROGRESS_TOTAL"

  if [ "$progress_done" -ge "$PROGRESS_TOTAL" ]; then
    printf '\n'
  fi
}

advance_progress() {
  progress_done=$((progress_done + 1))
  if [ "$progress_done" -gt "$PROGRESS_TOTAL" ]; then
    progress_done="$PROGRESS_TOTAL"
  fi
  draw_progress
}

report_ok() {
  printf '[ok] %s\n' "$1"
  ok_count=$((ok_count + 1))
  advance_progress
}

report_warn() {
  printf '[warn] %s\n' "$1"
  warn_count=$((warn_count + 1))
  advance_progress
}

report_fail() {
  printf '[fail] %s\n' "$1"
  fail_count=$((fail_count + 1))
  advance_progress
}

check_required_file() {
  file_path="$1"
  label="$2"
  if [ -e "$file_path" ]; then
    report_ok "$label"
  else
    report_fail "$label is missing: $file_path"
  fi
}

check_ldd() {
  binary_path="$1"
  label="$2"
  if [ ! -x "$binary_path" ]; then
    report_fail "$label is not executable: $binary_path"
    return
  fi

  missing_lines="$(ldd "$binary_path" 2>/dev/null | grep 'not found' || true)"
  if [ -n "$missing_lines" ]; then
    report_fail "$label has unresolved shared libraries"
    printf '%s\n' "$missing_lines" | sed 's/^/  - /'
    return
  fi

  report_ok "$label shared libraries resolve"
}

check_shell_script() {
  script_path="$1"
  label="$2"
  if [ ! -x "$script_path" ]; then
    report_fail "$label is not executable: $script_path"
    return
  fi

  if bash -n "$script_path" >/dev/null 2>&1; then
    report_ok "$label shell syntax is valid"
  else
    report_fail "$label shell syntax check failed: $script_path"
  fi
}

reset_run_state() {
  progress_done=0
  ok_count=0
  warn_count=0
  fail_count=0
}

run_checks() {
  reset_run_state

  echo "[check] root: $ROOT_DIR"
  echo "[check] host: $(uname -s 2>/dev/null || printf unknown)/$(uname -m 2>/dev/null || printf unknown)"
  draw_progress

  case "$(uname -s 2>/dev/null || printf unknown)" in
    Linux)
      report_ok "Linux host detected"
      ;;
    *)
      report_fail "This package expects Linux"
      ;;
  esac

  case "$(uname -m 2>/dev/null || printf unknown)" in
    x86_64|amd64)
      report_ok "x86_64 CPU architecture"
      ;;
    *)
      report_fail "Unsupported CPU architecture for the bundled binaries"
      ;;
  esac

  for tool in bash ldd awk sed grep cp chmod uname; do
    if have "$tool"; then
      report_ok "$tool available"
    else
      report_fail "$tool is missing"
    fi
  done

  if have nvidia-smi; then
    gpu_name="$(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | head -n 1 || true)"
    compute_cap="$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n 1 | tr -d '[:space:]' || true)"
    if [ -n "$gpu_name" ] || [ -n "$compute_cap" ]; then
      report_ok "NVIDIA driver/runtime detected"
      [ -n "$gpu_name" ] && printf '  - GPU: %s\n' "$gpu_name"
      [ -n "$compute_cap" ] && printf '  - compute_cap: %s\n' "$compute_cap"
    else
      report_warn "nvidia-smi exists but no GPU details were returned"
    fi
  else
    report_fail "nvidia-smi not found, so the NVIDIA driver/runtime is likely missing"
  fi

  check_required_file "$ROOT_DIR/bds-sim" "launcher"
  check_required_file "$ROOT_DIR/$BIN_DIR/bds-sim-fat" "fat binary"
  check_required_file "$ROOT_DIR/BRDM" "BRDM directory"
  check_required_file "$ROOT_DIR/fonts" "fonts directory"
  check_required_file "$ROOT_DIR/ne_50m_land" "map data directory"

  check_shell_script "$ROOT_DIR/bds-sim" "launcher"
  check_ldd "$ROOT_DIR/$BIN_DIR/bds-sim-fat" "fat binary"

  if [ -x "$ROOT_DIR/scripts/check-gpu-series-support.sh" ]; then
    report_ok "GPU series helper script present"
  else
    report_warn "GPU series helper script is missing"
  fi

  if [ -x "$ROOT_DIR/scripts/rebuild-local.sh" ]; then
    report_ok "rebuild helper script present"
  else
    report_warn "rebuild helper script is missing"
  fi

  if [ -x "$ROOT_DIR/scripts/portable-oneclick-install.sh" ]; then
    report_ok "one-click install helper script present"
  else
    report_warn "one-click install helper script is missing"
  fi

  echo
  printf '[check] summary: %d ok, %d warning(s), %d failure(s)\n' "$ok_count" "$warn_count" "$fail_count"
}
run_checks

if [ "$((fail_count + warn_count))" -gt 0 ] && [ "$prompt_allowed" -eq 1 ] && [ -x "$ROOT_DIR/scripts/portable-oneclick-install.sh" ] && [ -t 0 ]; then
  printf '[check] 偵測到缺項，是否現在一鍵安裝相依套件？ [Y/n] '
  read -r answer
  case "$answer" in
    ""|y|Y|yes|YES)
      bash "$ROOT_DIR/scripts/portable-oneclick-install.sh"
      prompt_allowed=0
      echo
      echo "[check] 重新執行自檢中..."
      run_checks
      ;;
  esac
fi

if [ "$fail_count" -eq 0 ]; then
  echo "[result] 可啟動"
else
  echo "[result] 仍缺少"
fi

if [ "$fail_count" -gt 0 ]; then
  exit 2
fi

exit 0