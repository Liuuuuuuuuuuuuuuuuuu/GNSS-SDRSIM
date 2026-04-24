#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

print_section() {
  echo
  echo "============================================================"
  echo "$1"
  echo "============================================================"
}
