#!/usr/bin/env bash
set -eu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
if [ -d "$SCRIPT_DIR/@BIN_DIR@" ]; then
  ROOT_DIR="$SCRIPT_DIR"
elif [ -d "$SCRIPT_DIR/../@BIN_DIR@" ]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  ROOT_DIR="$SCRIPT_DIR"
fi
BIN_DIR="@BIN_DIR@"
REBUILD_SCRIPT="@REBUILD_SCRIPT@"
FAT_BIN="$ROOT_DIR/$BIN_DIR/gnss-sim-fat"

RUNTIME_LIB_DIRS=""
if [ -d "$ROOT_DIR/lib" ]; then
  RUNTIME_LIB_DIRS="$ROOT_DIR/lib"
fi
if [ -d "$ROOT_DIR/lib64" ]; then
  RUNTIME_LIB_DIRS="${RUNTIME_LIB_DIRS:+$RUNTIME_LIB_DIRS:}$ROOT_DIR/lib64"
fi
if [ -n "$RUNTIME_LIB_DIRS" ]; then
  export LD_LIBRARY_PATH="$RUNTIME_LIB_DIRS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

launcher_name="$(basename "$0")"
case "$launcher_name" in
  gnss-sim*)
    export GNSS_DEFAULT_SIGNAL_MODE="mixed"
    ;;
  *)
    export GNSS_DEFAULT_SIGNAL_MODE="mixed"
    ;;
esac

print_rebuild_hint() {
  if [ -x "$ROOT_DIR/$REBUILD_SCRIPT" ]; then
    echo "[launcher] Quick rebuild: $ROOT_DIR/$REBUILD_SCRIPT" >&2
  else
    echo "[launcher] Rebuild with: make clean && make" >&2
  fi
}

# List wireless interfaces matching a prefix (e.g. "wlx"), sorted by name.
_list_ifaces() {
  local prefix="$1"
  ls /sys/class/net/ 2>/dev/null \
    | grep "^${prefix}" \
    | grep -v 'mon$' \
    | sort
}

# Return the first available interface from a newline-separated candidate list.
_first_iface() {
  for iface in $1; do
    [ -d "/sys/class/net/$iface" ] && { echo "$iface"; return; }
  done
}

auto_detect_three_nics() {
  case "${BDS_WIFI_RID_SKIP_AUTODETECT:-0}" in
    1|true|TRUE|True|yes|YES|on|ON)
      echo "[launcher] skip Wi-Fi RID interface auto-detect (BDS_WIFI_RID_SKIP_AUTODETECT=1)" >&2
      return
      ;;
  esac

  if [ -n "${BDS_WIFI_RID_IFACE_1:-}" ]; then
    return
  fi

  local ext_nics
  ext_nics=$(_list_ifaces "wlx")
  local ext1 ext2 ext3
  ext1=$(echo "$ext_nics" | sed -n '1p')
  ext2=$(echo "$ext_nics" | sed -n '2p')
  ext3=$(echo "$ext_nics" | sed -n '3p')

  local int_mon
  int_mon=$(_first_iface "$(ls /sys/class/net/ 2>/dev/null | grep '^wlp' | grep 'mon$' | sort)")

  if [ -n "$ext1" ] && [ -n "$ext2" ] && [ -n "$ext3" ] && [ -n "$int_mon" ]; then
    BDS_WIFI_RID_IFACE_1="$ext1"
    BDS_WIFI_RID_IFACE_2="$ext2"
    BDS_WIFI_RID_IFACE_3="$ext3"
    BDS_WIFI_RID_IFACE_4="$int_mon"
    BDS_WIFI_RID_IFACE_2_CHANNELS="2,3,4,5,7"
    BDS_WIFI_RID_IFACE_4_CHANNELS="8,9,10,12,13"
    BDS_WIFI_RID_SOURCE="legacy"
    echo "[launcher] 4-NIC: searcher1=$ext1 searcher2=$ext2 tracker=$ext3 aux=$int_mon" >&2
    export BDS_WIFI_RID_IFACE_1 BDS_WIFI_RID_IFACE_2 BDS_WIFI_RID_IFACE_3 BDS_WIFI_RID_IFACE_4
    export BDS_WIFI_RID_IFACE_2_CHANNELS BDS_WIFI_RID_IFACE_4_CHANNELS
    return
  fi

  if [ -n "$ext1" ] && [ -n "$ext2" ] && [ -n "$int_mon" ]; then
    BDS_WIFI_RID_IFACE_1="$ext1"
    BDS_WIFI_RID_IFACE_2="$ext2"
    BDS_WIFI_RID_IFACE_3="$int_mon"
    BDS_WIFI_RID_SOURCE="legacy"
    echo "[launcher] 3-NIC: searcher=$ext1 tracker=$ext2 aux=$int_mon" >&2
    export BDS_WIFI_RID_IFACE_1 BDS_WIFI_RID_IFACE_2 BDS_WIFI_RID_IFACE_3
    return
  fi

  if [ -n "$ext1" ] && [ -n "$int_mon" ]; then
    BDS_WIFI_RID_IFACE_1="$ext1"
    BDS_WIFI_RID_IFACE_3="$int_mon"
    BDS_WIFI_RID_SOURCE="legacy"
    echo "[launcher] 2-NIC mode: searcher=$ext1 tracker=$int_mon" >&2
    export BDS_WIFI_RID_IFACE_1 BDS_WIFI_RID_IFACE_3
    return
  fi

  if [ -n "$ext1" ]; then
    BDS_WIFI_RID_IFACE="$ext1"
    BDS_WIFI_RID_SOURCE="legacy"
    echo "[launcher] single-NIC mode (external): $ext1" >&2
    export BDS_WIFI_RID_IFACE
    return
  fi

  if [ -n "$int_mon" ]; then
    BDS_WIFI_RID_IFACE="$int_mon"
    echo "[launcher] single-NIC mode (internal monitor): $int_mon" >&2
    export BDS_WIFI_RID_IFACE
    return
  fi

  echo "[launcher] no wireless interface detected; Wi-Fi RID bridge will be skipped." >&2
}

apply_runtime_defaults() {
  : "${BDS_WIFI_RID_SKIP_AUTODETECT:=1}"
  auto_detect_three_nics

  if [ -n "${BDS_WIFI_RID_IFACE_1:-}" ]; then
    : "${BDS_WIFI_RID_SOURCE:=legacy}"
  else
    : "${BDS_WIFI_RID_SOURCE:=wireshark}"
  fi

  : "${BDS_WIFI_RID_HOP_MS:=140}"
  : "${BDS_WIFI_RID_TRACKER_POLL_MS:=25}"
  : "${BDS_WIFI_RID_TRACKER_RECV_TIMEOUT_MS:=50}"
  : "${BDS_WIFI_RID_TRACKER_STALE_MS:=3000}"
  : "${BDS_WIFI_RID_IPC_MIN_RSSI:=-82}"
  : "${BDS_WIFI_RID_IPC_SWITCH_DELTA_DB:=6}"
  : "${BDS_WIFI_RID_IPC_ARB_WINDOW_MS:=300}"

  : "${BDS_WIFI_RID_MIXED_ENABLE:=1}"
  if [ -z "${BDS_WIFI_RID_WIFI_MODE:-}" ]; then
    case "${BDS_WIFI_RID_MIXED_ENABLE}" in
      0|false|FALSE|False|no|NO|No)
        BDS_WIFI_RID_WIFI_MODE="rid"
        ;;
      *)
        BDS_WIFI_RID_WIFI_MODE="mixed"
        ;;
    esac
  fi
  : "${BDS_WIFI_RID_USE_SUDO:=0}"
  : "${BDS_BLE_RID_USE_SUDO:=0}"
  : "${BDS_BLE_RID_TRY_WIDE:=0}"

  export BDS_WIFI_RID_SOURCE
  export BDS_WIFI_RID_HOP_MS
  export BDS_WIFI_RID_TRACKER_POLL_MS
  export BDS_WIFI_RID_TRACKER_RECV_TIMEOUT_MS
  export BDS_WIFI_RID_TRACKER_STALE_MS
  export BDS_WIFI_RID_IPC_MIN_RSSI
  export BDS_WIFI_RID_IPC_SWITCH_DELTA_DB
  export BDS_WIFI_RID_IPC_ARB_WINDOW_MS
  export BDS_WIFI_RID_MIXED_ENABLE
  export BDS_WIFI_RID_WIFI_MODE
  export BDS_WIFI_RID_USE_SUDO
  export BDS_BLE_RID_USE_SUDO
  export BDS_BLE_RID_TRY_WIDE
  export BDS_WIFI_RID_SKIP_AUTODETECT
}

should_preauth_sudo() {
  local want_wifi="${BDS_WIFI_RID_USE_SUDO:-1}"
  local want_ble="${BDS_BLE_RID_USE_SUDO:-1}"

  case "$want_wifi" in
    0|false|FALSE|False|no|NO|No) ;;
    *) return 0 ;;
  esac
  case "$want_ble" in
    0|false|FALSE|False|no|NO|No) ;;
    *) return 0 ;;
  esac
  return 1
}

preauth_sudo_once() {
  if ! should_preauth_sudo; then
    return 0
  fi
  if ! command -v sudo >/dev/null 2>&1; then
    echo "[launcher] sudo not found; skipping startup pre-authorization." >&2
    return 0
  fi
  if sudo -n -v >/dev/null 2>&1; then
    export BDS_WIFI_RID_SUDO_NONINTERACTIVE="${BDS_WIFI_RID_SUDO_NONINTERACTIVE:-0}"
    export BDS_BLE_RID_SUDO_NONINTERACTIVE="${BDS_BLE_RID_SUDO_NONINTERACTIVE:-0}"
    return 0
  fi

  echo "[launcher] sudo authorization required for Wi-Fi/BLE monitor startup." >&2
  if ! sudo -v; then
    echo "[launcher] sudo authorization failed; aborting startup." >&2
    exit 1
  fi

  export BDS_WIFI_RID_SUDO_NONINTERACTIVE="${BDS_WIFI_RID_SUDO_NONINTERACTIVE:-0}"
  export BDS_BLE_RID_SUDO_NONINTERACTIVE="${BDS_BLE_RID_SUDO_NONINTERACTIVE:-0}"
}

auto_fix_ble_hci() {
  : "${BDS_BLE_RID_AUTO_UNBLOCK:=0}"
  : "${BDS_BLE_RID_STOP_BLUETOOTHD:=1}"
  case "${BDS_BLE_RID_USE_SUDO:-0}" in
    0|false|FALSE|False|no|NO|No)
      return 0
      ;;
  esac
  case "$BDS_BLE_RID_AUTO_UNBLOCK" in
    0|false|FALSE|False|no|NO|No)
      return 0
      ;;
  esac

  local has_hci=0
  [ -d /sys/class/bluetooth/hci0 ] && has_hci=1
  if [ "$has_hci" -eq 0 ] && command -v hciconfig >/dev/null 2>&1; then
    hciconfig -a 2>/dev/null | grep -q '^hci0:' && has_hci=1 || true
  fi
  [ "$has_hci" -eq 0 ] && return 0

  case "$BDS_BLE_RID_STOP_BLUETOOTHD" in
    0|false|FALSE|False|no|NO|No) ;;
    *)
      if command -v systemctl >/dev/null 2>&1; then
        if command -v sudo >/dev/null 2>&1; then
          sudo systemctl stop bluetooth >/dev/null 2>&1 || true
        else
          systemctl stop bluetooth >/dev/null 2>&1 || true
        fi
      fi
      ;;
  esac

  if command -v rfkill >/dev/null 2>&1; then
    if rfkill list 2>/dev/null | grep -A2 -E '(^|[[:space:]])hci[0-9]+:|Bluetooth' | grep -qi 'Soft blocked: yes'; then
      if command -v sudo >/dev/null 2>&1; then
        sudo rfkill unblock bluetooth >/dev/null 2>&1 || true
        sudo rfkill unblock all >/dev/null 2>&1 || true
      else
        rfkill unblock bluetooth >/dev/null 2>&1 || true
      fi
      echo "[launcher] BLE auto-fix: unblocked bluetooth soft block." >&2
    fi
  fi

  if command -v hciconfig >/dev/null 2>&1; then
    if hciconfig hci0 2>/dev/null | grep -q 'DOWN'; then
      if command -v sudo >/dev/null 2>&1; then
        sudo hciconfig hci0 up >/dev/null 2>&1 || true
      else
        hciconfig hci0 up >/dev/null 2>&1 || true
      fi
      echo "[launcher] BLE auto-fix: attempted 'hciconfig hci0 up'." >&2
    fi
  fi
}

apply_runtime_defaults
preauth_sudo_once
auto_fix_ble_hci

if ! command -v nvidia-smi >/dev/null 2>&1; then
  echo "[launcher] nvidia-smi not found. NVIDIA driver/runtime may be missing." >&2
fi

gpu_cc=""
if command -v nvidia-smi >/dev/null 2>&1; then
  gpu_cc=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader 2>/dev/null | head -n 1 | tr -d "[:space:]" || true)
fi

if [ -x "$FAT_BIN" ]; then
  if ldd "$FAT_BIN" 2>/dev/null | grep -q "not found"; then
    echo "[launcher] Missing runtime libraries for $FAT_BIN:" >&2
    ldd "$FAT_BIN" 2>/dev/null | grep "not found" >&2 || true
    print_rebuild_hint
    exit 2
  fi
  echo "[launcher] Using gnss-sim-fat (compute_cap=${gpu_cc:-unknown})" >&2
  exec "$FAT_BIN" "$@"
fi

for bin in gnss-sim-modern gnss-sim-ada gnss-sim-ampere gnss-sim-turing gnss-sim-blackwell gnss-sim-pascal; do
  if [ -x "$ROOT_DIR/$BIN_DIR/$bin" ]; then
    echo "[launcher] Using fallback $bin (compute_cap=${gpu_cc:-unknown})" >&2
    exec "$ROOT_DIR/$BIN_DIR/$bin" "$@"
  fi
done

echo "[launcher] No runnable gnss-sim binary found under $ROOT_DIR/$BIN_DIR." >&2
print_rebuild_hint
exit 2
