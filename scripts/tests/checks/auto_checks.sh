#!/usr/bin/env bash
set -euo pipefail

run_auto_checks() {
  print_section "Auto Checks"
  echo "[check] make -j1 build/main.o"
  make -j1 build/main.o >/dev/null

  echo "[check] make -j1 build/main_gui.o"
  make -j1 build/main_gui.o >/dev/null

  echo "[check] launcher help"
  BDS_WIFI_RID_USE_SUDO=0 \
  BDS_BLE_RID_USE_SUDO=0 \
  ./bds-sim --help >/dev/null

  echo "[ok] core C/C++ checks passed"
}
