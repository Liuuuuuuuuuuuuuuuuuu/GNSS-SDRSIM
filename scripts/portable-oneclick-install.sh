#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)"

have() {
  command -v "$1" >/dev/null 2>&1
}

confirm() {
  prompt="$1"
  printf '%s [Y/n] ' "$prompt"
  read -r answer
  case "$answer" in
    ""|y|Y|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

run_with_sudo() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  else
    sudo "$@"
  fi
}

install_with_apt() {
  echo "[install] detected package manager: apt"

  if confirm "是否安裝常見執行期套件 (bash/coreutils/grep/sed/gawk/libstdc++6/libgcc-s1/libc6/pciutils)？"; then
    run_with_sudo apt update
    run_with_sudo apt install -y bash coreutils grep sed gawk libstdc++6 libgcc-s1 libc6 pciutils
  fi

  if ! have nvidia-smi; then
    if confirm "偵測不到 nvidia-smi，是否嘗試安裝 NVIDIA driver？"; then
      run_with_sudo apt install -y ubuntu-drivers-common || true
      if have ubuntu-drivers; then
        run_with_sudo ubuntu-drivers autoinstall || true
      else
        run_with_sudo apt install -y nvidia-driver-550 || run_with_sudo apt install -y nvidia-driver-535 || true
      fi
    fi
  fi

  if [ ! -x "$ROOT_DIR/scripts/rebuild-local.sh" ]; then
    return 0
  fi
}

install_with_dnf() {
  echo "[install] detected package manager: dnf"
  if confirm "是否安裝常見執行期套件 (bash/coreutils/grep/sed/gawk/libstdc++/glibc/pciutils)？"; then
    run_with_sudo dnf install -y bash coreutils grep sed gawk libstdc++ glibc pciutils
  fi
  if ! have nvidia-smi; then
    echo "[install] 未偵測到 nvidia-smi。Fedora/RHEL 的 NVIDIA 驅動通常需先啟用對應 repository (如 rpmfusion)。"
  fi
}

install_with_pacman() {
  echo "[install] detected package manager: pacman"
  if confirm "是否安裝常見執行期套件 (bash/coreutils/grep/sed/gawk/gcc-libs/glibc/pciutils)？"; then
    run_with_sudo pacman -Sy --noconfirm bash coreutils grep sed gawk gcc-libs glibc pciutils
  fi
  if ! have nvidia-smi; then
    if confirm "是否嘗試安裝 NVIDIA driver 套件 (nvidia-utils, nvidia)？"; then
      run_with_sudo pacman -S --noconfirm nvidia-utils nvidia || true
    fi
  fi
}

install_with_zypper() {
  echo "[install] detected package manager: zypper"
  if confirm "是否安裝常見執行期套件 (bash/coreutils/grep/sed/gawk/libstdc++6/glibc/pciutils)？"; then
    run_with_sudo zypper --non-interactive install bash coreutils grep sed gawk libstdc++6 glibc pciutils
  fi
  if ! have nvidia-smi; then
    echo "[install] 未偵測到 nvidia-smi。openSUSE 可能需先安裝/啟用 NVIDIA repository。"
  fi
}

echo "[install] root: $ROOT_DIR"

if have apt; then
  install_with_apt
elif have dnf; then
  install_with_dnf
elif have pacman; then
  install_with_pacman
elif have zypper; then
  install_with_zypper
else
  echo "[install] unsupported package manager. Please install dependencies manually."
  exit 2
fi

echo "[install] done. 建議再執行一次 ./scripts/portable-self-check.sh 重新檢查。"