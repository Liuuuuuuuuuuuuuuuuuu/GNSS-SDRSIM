@echo off
setlocal

set "WSL_DISTRO="

where wsl.exe >nul 2>&1
if errorlevel 1 (
  echo [ERROR] WSL is not installed or not in PATH.
  echo Please install WSL first, then run this installer again.
  pause
  exit /b 1
)

for /f "usebackq delims=" %%D in (`wsl.exe -l -q ^| findstr /R /I "^Ubuntu"`) do (
  if not defined WSL_DISTRO set "WSL_DISTRO=%%D"
)

if not defined WSL_DISTRO (
  echo [ERROR] Ubuntu WSL distro was not found.
  echo Please install Ubuntu in WSL, then run this installer again.
  pause
  exit /b 1
)

for %%I in ("%~dp0.") do set "WIN_ROOT=%%~fI"

echo [INFO] Installing GNSS desktop shortcut via WSL distro: %WSL_DISTRO%

wsl.exe -d "%WSL_DISTRO%" bash -lc "set -e; ROOT_LINUX=\"\$(wslpath -u '%WIN_ROOT%')\"; cd \"\$ROOT_LINUX\"; bash ./Install-GNSS.sh"
if errorlevel 1 (
  echo [ERROR] Installation failed.
  echo Make sure Ubuntu WSL is installed and can run bash.
  pause
  exit /b 1
)

echo [OK] Installation complete (Windows + WSL Ubuntu). Check your desktop for GNSS icon.
pause
exit /b 0
