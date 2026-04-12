#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

mode="${1:-auto}"

print_section() {
  echo
  echo "============================================================"
  echo "$1"
  echo "============================================================"
}

manual_checklist() {
  print_section "Manual Runtime Checklist (C/C++ package)"
  cat <<'EOF'
[ ] Build passes: make -j$(nproc)
[ ] Launcher works: ./bds-sim --help
[ ] Crossbow mode toggles in GUI
[ ] RID diagnostics line updates when UDP JSON arrives (if bridge exists)
[ ] Radar point appears when gate=PASSED
EOF
}

auto_checks() {
  print_section "Auto Checks"
  echo "[check] make -j1 build/main.o"
  make -j1 build/main.o >/dev/null

  echo "[check] make -j1 build/main_gui.o"
  make -j1 build/main_gui.o >/dev/null

  echo "[check] launcher help"
  ./bds-sim --help >/dev/null

  echo "[ok] core C/C++ checks passed"
}

case "$mode" in
  auto)
    auto_checks
    manual_checklist
    ;;
  manual)
    manual_checklist
    ;;
  *)
    echo "Usage: $0 [auto|manual]" >&2
    exit 1
    ;;
esac
