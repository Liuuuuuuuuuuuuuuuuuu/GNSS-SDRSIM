#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/checks/auto_checks.sh"
source "$SCRIPT_DIR/checks/manual_checklist.sh"

cd "$ROOT_DIR"

mode="${1:-auto}"

case "$mode" in
  auto)
    run_auto_checks
    print_manual_checklist
    ;;
  manual)
    print_manual_checklist
    ;;
  *)
    echo "Usage: $0 [auto|manual]" >&2
    exit 1
    ;;
esac
