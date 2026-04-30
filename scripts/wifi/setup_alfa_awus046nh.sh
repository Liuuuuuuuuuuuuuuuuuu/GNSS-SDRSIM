#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   sudo scripts/wifi/setup_alfa_awus046nh.sh [iface]
# Then run:
#   export BDS_WIFI_RID_IFACE=<iface-or-ifacemon>
#   ./gnss-sim

detect_iface() {
  local n dev_path modalias

  # Prefer interfaces already in monitor mode.
  if command -v iw >/dev/null 2>&1; then
    local monitor_iface
    monitor_iface="$(iw dev 2>/dev/null | awk '/Interface/ {i=$2} /type monitor/ {print i; exit}')"
    if [[ -n "$monitor_iface" ]]; then
      echo "$monitor_iface"
      return 0
    fi
  fi

  # Prefer external USB sniffing chipsets: RTL8187 and RT3070/2870 family.
  for n in /sys/class/net/*; do
    n="$(basename "$n")"
    [[ "$n" == "lo" ]] && continue
    dev_path="/sys/class/net/${n}/device/modalias"
    if [[ -f "$dev_path" ]]; then
      modalias="$(cat "$dev_path" 2>/dev/null || true)"
      if [[ "$modalias" == *"v0BDAp8187"* || "$modalias" == *"v148Fp3070"* || "$modalias" == *"v148Fp2870"* ]]; then
        echo "$n"
        return 0
      fi
    fi
  done

  # Fallback: first interface reported by iw dev
  if command -v iw >/dev/null 2>&1; then
    iw dev 2>/dev/null | awk '/Interface/ {print $2; exit}'
    return 0
  fi

  return 1
}

IFACE="${1:-}"
if [[ -z "$IFACE" ]]; then
  IFACE="$(detect_iface || true)"
fi
if [[ -z "$IFACE" ]]; then
  echo "[setup] cannot detect Wi-Fi interface automatically" >&2
  echo "[setup] usage: sudo scripts/wifi/setup_alfa_awus046nh.sh <iface>" >&2
  exit 1
fi

if [[ "$IFACE" == *mon ]]; then
  MON_IFACE="$IFACE"
else
  MON_IFACE="${IFACE}mon"
fi

if ! command -v iw >/dev/null 2>&1; then
  echo "[setup] missing 'iw'" >&2
  exit 1
fi

if ! command -v ip >/dev/null 2>&1; then
  echo "[setup] missing 'ip'" >&2
  exit 1
fi

echo "[setup] bring down ${IFACE}"
ip link set "${IFACE}" down

if [[ "$MON_IFACE" != "$IFACE" ]] && ! ip link show "${MON_IFACE}" >/dev/null 2>&1; then
  echo "[setup] create monitor interface ${MON_IFACE}"
  if ! iw dev "${IFACE}" interface add "${MON_IFACE}" type monitor; then
    echo "[setup] fallback: set ${IFACE} type monitor"
    iw dev "${IFACE}" set type monitor
  fi
fi

echo "[setup] bring up ${IFACE}"
ip link set "${IFACE}" up
if ip link show "${MON_IFACE}" >/dev/null 2>&1; then
  echo "[setup] bring up ${MON_IFACE}"
  ip link set "${MON_IFACE}" up
fi

# Some drivers expose IFACE directly, others create IFACEmon.
if ip link show "${MON_IFACE}" >/dev/null 2>&1; then
  USE_IFACE="${MON_IFACE}"
else
  USE_IFACE="${IFACE}"
fi

echo "[setup] monitor interface ready: ${USE_IFACE}"
echo ""
echo "export BDS_WIFI_RID_IFACE=${USE_IFACE}"
echo "# optional: export BDS_WIFI_RID_BRIDGE_BIN=bin/wifi-rid-bridge"
echo ""
echo "Then run:"
echo "  ./gnss-sim"
