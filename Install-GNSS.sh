#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

if [[ ! -r /etc/os-release ]]; then
	echo "[ERROR] Unsupported Linux distribution. Ubuntu is required." >&2
	exit 1
fi

. /etc/os-release
if [[ "${ID:-}" != "ubuntu" && "${ID_LIKE:-}" != *ubuntu* ]]; then
	echo "[ERROR] This installer supports Ubuntu only." >&2
	echo "[ERROR] Detected: ${PRETTY_NAME:-unknown}" >&2
	exit 1
fi

bash scripts/install/install-desktop-icon.sh

echo
echo "Installation complete (Ubuntu). You can now launch GNSS from your desktop icon."
