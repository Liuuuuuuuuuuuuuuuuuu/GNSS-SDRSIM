#!/usr/bin/env bash

# One-shot USRP readiness check for B210.
# Read-only diagnostics: no firmware flashing, no reset, no config changes.

set -u

PASS=0
WARN=0
FAIL=0

pass() {
  echo "[PASS] $1"
  PASS=$((PASS + 1))
}

warn() {
  echo "[WARN] $1"
  WARN=$((WARN + 1))
}

fail() {
  echo "[FAIL] $1"
  FAIL=$((FAIL + 1))
}

section() {
  echo
  echo "=== $1 ==="
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

extract_b210_speed() {
  # Print numeric speed token from sysfs (e.g. 5000M / 480M / 12M)
  # for 2500:0020 (runtime) or 2500:0021 (bootloader).
  local d vid pid spd
  for d in /sys/bus/usb/devices/*; do
    [[ -f "$d/idVendor" && -f "$d/idProduct" && -f "$d/speed" ]] || continue
    vid=$(cat "$d/idVendor" 2>/dev/null || true)
    pid=$(cat "$d/idProduct" 2>/dev/null || true)
    if [[ "$vid" == "2500" && ("$pid" == "0020" || "$pid" == "0021") ]]; then
      spd=$(cat "$d/speed" 2>/dev/null || true)
      if [[ "$spd" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
        # Keep output format compatible with old checks.
        if [[ "$spd" == *.* ]]; then
          printf "%sM\n" "${spd%.*}"
        else
          printf "%sM\n" "$spd"
        fi
        return 0
      fi
    fi
  done
  return 1
}

section "Environment"
echo "Host: $(hostname)"
echo "Kernel: $(uname -r)"

section "USB Presence"
if ! have_cmd lsusb; then
  fail "lsusb not found (install usbutils)"
else
  B210_LINE=$(lsusb | grep -E '2500:0020|2500:0021' | head -1 || true)
  if [[ -n "$B210_LINE" ]]; then
    pass "USRP detected on USB: $B210_LINE"
  else
    fail "USRP B210 not found on USB (expect 2500:0020 or 2500:0021)"
  fi
fi

section "USB Link Speed"
if have_cmd lsusb; then
  B210_SPEED=$(extract_b210_speed || true)
  if [[ -z "$B210_SPEED" ]]; then
    warn "Could not determine B210 link speed from lsusb -t"
  else
    echo "B210 link speed: $B210_SPEED"
    if [[ "$B210_SPEED" =~ ^[0-9]+M$ ]]; then
      SPEED_VAL=${B210_SPEED%M}
      if (( SPEED_VAL >= 5000 )); then
        pass "B210 is on USB 3.x (>=5000M)"
      elif (( SPEED_VAL >= 480 )); then
        fail "B210 is on USB 2.0 ($B210_SPEED). Move to USB 3.x port/cable."
      else
        fail "B210 link speed is too low ($B210_SPEED)"
      fi
    else
      warn "Unexpected speed format: $B210_SPEED"
    fi
  fi
fi

section "Process and FD Hygiene"
BDS_PROCS=$(ps -eo pid,stat,cmd | grep -E 'gnss-sim( |$)|gnss-sim-fat( |$)|gnss-sim-pascal( |$)|gnss-sim-turing( |$)|gnss-sim-ampere( |$)|gnss-sim-ada( |$)|gnss-sim-blackwell( |$)|gnss-sim-modern( |$)' | grep -v grep || true)
if [[ -n "$BDS_PROCS" ]]; then
  warn "Found running gnss-sim processes:"
  echo "$BDS_PROCS"
else
  pass "No gnss-sim process is currently running"
fi

USB_HOLDERS=$(for p in /proc/[0-9]*; do
  pid=${p##*/}
  ls -l "$p/fd" 2>/dev/null | grep -q '/dev/bus/usb' && printf "%s %s\n" "$pid" "$(tr -d '\0' </proc/$pid/cmdline)"
done | sed '/^[0-9]\+ $/d' || true)
if [[ -n "$USB_HOLDERS" ]]; then
  warn "Processes currently holding USB device nodes:"
  echo "$USB_HOLDERS" | head -20
else
  pass "No process currently holds /dev/bus/usb handles"
fi

section "UHD Checks"
if ! have_cmd uhd_find_devices; then
  fail "uhd_find_devices not found (install UHD host tools)"
else
  UHD_FIND=$(uhd_find_devices 2>&1)
  if echo "$UHD_FIND" | grep -q -E 'Device Address:'; then
    pass "uhd_find_devices can enumerate at least one UHD device"
  else
    fail "uhd_find_devices failed to enumerate device"
    echo "$UHD_FIND" | sed -n '1,30p'
  fi
fi

if have_cmd uhd_usrp_probe; then
  UHD_PROBE=$(timeout 15 uhd_usrp_probe --args 'type=b200' 2>&1 || true)
  if echo "$UHD_PROBE" | grep -q -E 'AssertionError: accum_timeout < _timeout'; then
    fail "uhd_usrp_probe hit b200 wait_for_ack timeout"
  elif echo "$UHD_PROBE" | grep -q -E 'No UHD Devices Found|No devices found|Error:'; then
    fail "uhd_usrp_probe reports device init failure"
  else
    pass "uhd_usrp_probe did not report known fatal errors"
  fi
  echo "Probe summary (first 20 lines):"
  echo "$UHD_PROBE" | sed -n '1,20p'
else
  warn "uhd_usrp_probe not found; skipped deep probe"
fi

section "Summary"
echo "PASS=$PASS WARN=$WARN FAIL=$FAIL"

if (( FAIL > 0 )); then
  echo "RESULT: NOT READY"
  exit 2
fi

if (( WARN > 0 )); then
  echo "RESULT: READY WITH WARNINGS"
  exit 1
fi

echo "RESULT: READY"
exit 0
