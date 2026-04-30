#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LAUNCH_SCRIPT="$ROOT_DIR/scripts/entrypoints/gnss-desktop-launch.sh"
ICON_PATH="$ROOT_DIR/assets/gnss-logo.png"

is_wsl() {
  if [[ -n "${WSL_DISTRO_NAME:-}" ]]; then
    return 0
  fi
  if grep -qi microsoft /proc/version 2>/dev/null; then
    return 0
  fi
  return 1
}

install_linux_shortcut() {
  local desktop_dir="$HOME/Desktop"
  local app_dir="$HOME/.local/share/applications"
  local desktop_file="$desktop_dir/GNSS.desktop"
  local app_file="$app_dir/gnss.desktop"

  mkdir -p "$desktop_dir" "$app_dir"

  cat > "$desktop_file" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=GNSS
Comment=Launch GNSS mixed (GPS+BDS) mode
Exec=$LAUNCH_SCRIPT
Path=$ROOT_DIR
Terminal=false
Categories=Science;Utility;
StartupWMClass=gnss
Icon=$ICON_PATH
EOF

  chmod +x "$desktop_file"
  cp "$desktop_file" "$app_file"

  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$app_dir" >/dev/null 2>&1 || true
  fi

  echo "[install] Linux desktop shortcut created: $desktop_file"
}

install_wsl_windows_shortcut() {
  if ! command -v powershell.exe >/dev/null 2>&1; then
    echo "[install] powershell.exe not found. Cannot create Windows desktop shortcut from WSL." >&2
    exit 1
  fi

  local distro="${WSL_DISTRO_NAME:-}"
  if [[ -z "$distro" ]]; then
    distro="Ubuntu"
  fi

  local ps1
  ps1="$(mktemp /tmp/gnss-shortcut-XXXXXX.ps1)"

  cat > "$ps1" <<'PS1'
param(
  [Parameter(Mandatory = $true)][string]$Distro,
  [Parameter(Mandatory = $true)][string]$LinuxRoot
)

$desktop = [Environment]::GetFolderPath("Desktop")
$shortcutPath = Join-Path $desktop "GNSS.lnk"
$wslExe = "$env:SystemRoot\System32\wsl.exe"

$escapedLinuxRoot = $LinuxRoot.Replace("'", "''")
$bashCmd = "cd '$escapedLinuxRoot' && ./scripts/entrypoints/gnss-desktop-launch.sh"

$wsh = New-Object -ComObject WScript.Shell
$sc = $wsh.CreateShortcut($shortcutPath)
$sc.TargetPath = $wslExe
$sc.Arguments = "-d $Distro bash -lc `"$bashCmd`""
$sc.WorkingDirectory = $desktop
$sc.IconLocation = "$wslExe,0"
$sc.Description = "Launch GNSS-SDRSIM from WSL"
$sc.Save()

Write-Output $shortcutPath
PS1

  local out
  out="$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$ps1" -Distro "$distro" -LinuxRoot "$ROOT_DIR" | tr -d '\r')"
  rm -f "$ps1"

  echo "[install] Windows desktop shortcut created: $out"
}

main() {
  if [[ ! -x "$LAUNCH_SCRIPT" ]]; then
    echo "[install] launcher not executable or missing: $LAUNCH_SCRIPT" >&2
    exit 1
  fi

  if is_wsl; then
    install_wsl_windows_shortcut
  else
    install_linux_shortcut
  fi

  echo "[install] done"
}

main "$@"
