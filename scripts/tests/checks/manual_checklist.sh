#!/usr/bin/env bash
set -euo pipefail

print_manual_checklist() {
  print_section "Manual Runtime Checklist (C/C++ package)"
  cat <<'EOF'
[ ] Build passes: make -j$(nproc)
[ ] Launcher works: ./gnss-sim --help
[ ] Crossbow mode toggles in GUI
[ ] RID diagnostics line updates when UDP JSON arrives (if bridge exists)
[ ] Radar point appears when gate=PASSED
[ ] Tutorial/guide overlay opens and step transitions work (next/back/highlight)
[ ] Gear single-tap opens control page after 300ms idle
[ ] Gear rapid multi-tap (10x) triggers crossbow unlock flow
[ ] Control editor Page switch can edit both Simple and Detail panels
[ ] Appearance scope: selected element changes only itself; no selection changes all
EOF
}
