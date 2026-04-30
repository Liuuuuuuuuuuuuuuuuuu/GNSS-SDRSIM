#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG_DIR="$ROOT_DIR/runtime_logs"
mkdir -p "$LOG_DIR"

DURATION_SEC="${1:-0}"
LOG_FILE="$LOG_DIR/map-perf-$(date +%Y%m%d-%H%M%S).log"

cd "$ROOT_DIR"

echo "[map-prof] root: $ROOT_DIR"
echo "[map-prof] log:  $LOG_FILE"

echo "[map-prof] enabling built-in map profiler (BDS_GUI_MAP_PERF=1)"
export BDS_GUI_MAP_PERF=1

if [[ "$DURATION_SEC" =~ ^[0-9]+$ ]] && [[ "$DURATION_SEC" -gt 0 ]]; then
  echo "[map-prof] running for ${DURATION_SEC}s"
  timeout "${DURATION_SEC}s" gui 2>&1 | tee "$LOG_FILE" || true
else
  echo "[map-prof] running until GUI exits (Ctrl+C to stop)"
  gui 2>&1 | tee "$LOG_FILE"
fi

echo "[map-prof] last realtime map metrics:"
rg "\[perf\]\[map\]" "$LOG_FILE" | tail -n 20 || true
