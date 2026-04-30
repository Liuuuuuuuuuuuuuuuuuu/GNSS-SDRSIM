#!/usr/bin/env bash
set -euo pipefail

# One-shot automation:
# 1) auto detect Wi-Fi iface (or use first arg)
# 2) switch to monitor mode
# 3) export Wi-Fi RID env
# 4) run ./gnss-sim

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/../.." && pwd)"
IFACE_ARG="${1:-}"
shift || true

if ! command -v sudo >/dev/null 2>&1; then
  echo "[auto] missing sudo" >&2
  exit 1
fi

echo "[auto] sudo pre-auth"
sudo -v

SETUP_OUT="$(sudo "$ROOT_DIR/scripts/wifi/setup_alfa_awus046nh.sh" ${IFACE_ARG:+"$IFACE_ARG"})"
echo "$SETUP_OUT"

RID_IFACE="$(echo "$SETUP_OUT" | awk -F= '/^export BDS_WIFI_RID_IFACE=/{print $2; exit}')"
if [[ -z "$RID_IFACE" ]]; then
  echo "[auto] failed to parse monitor interface from setup output" >&2
  exit 1
fi

export BDS_WIFI_RID_IFACE="$RID_IFACE"
export BDS_WIFI_RID_USE_SUDO=1

echo "[auto] starting gnss-sim with BDS_WIFI_RID_IFACE=${BDS_WIFI_RID_IFACE}"
exec "$ROOT_DIR/gnss-sim" "$@"
