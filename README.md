### 🌍 Language / 語言

[繁體中文](README.zh.md) | **English**

---

# GNSS-SDRSIM Release & User Guide

This documentation covers two deployment scenarios:

- **Source Code Package**: For those who want to compile, modify, or review the source code.
- **Portable Package**: For those who just want to extract and run directly.

For quick start, see [Portable Quick Start](#portable-quick-start).

## Table of Contents

- [Which Package to Download](#which-package-to-download)
- [System-Level Requirements](#system-level-requirements)
- [Installation Requirements](#installation-requirements)
- [Source Code Compilation](#source-code-compilation)
- [Portable Quick Start](#portable-quick-start)
- [Portable Package Dependencies](#portable-package-dependencies)
- [Complete Workflow](#complete-workflow)
- [FAQ](#faq)

## Which Package to Download

### Source Code Package

The source code package contains complete project files, suitable for:

- Those who want to compile it themselves
- Those who want to modify functionality
- Those who want to review the source code

After download, you need to install dependencies, run `make`, then execute `./bds-sim`.

### Portable Package

The portable package is a pre-compiled release version, suitable for:

- Those who just want to run it
- Those who don't want to deal with compilation environment
- Those who want to distribute to other users

After extraction, you can usually just run `./bds-sim` directly.

The portable package typically does not include complete source code, so users won't see your `.c`, `.cpp`, `.cu`, `.h` files.

## System-Level Requirements

This program has specific environment requirements and cannot run cross-platform.

### Hardware Requirements

| Item | Requirement | Notes |
|------|-------------|-------|
| **GPU** | NVIDIA Discrete GPU | Required. Intel/AMD GPU not supported yet. |
| **GPU Architecture** | Pascal (P40) or newer | Supports Pascal, Turing, Ampere, Ada, Blackwell. Auto-selects compatible version. |
| **VRAM** | At least 2 GB | Depends on simulation scenario complexity. |

### Operating System Requirements

| Item | Requirement | Notes |
|------|-------------|-------|
| **OS** | Linux | Ubuntu 20.04 LTS / 22.04 LTS recommended. WSL2 (Ubuntu) also supported. |
| **NVIDIA Driver** | Installed | Must be compatible with CUDA 11.x/12.x. |
| **X11/Wayland** | Built-in | Desktop environment must have graphics server. Headless environments need Xvfb. |
| **glibc** | 2.31+ | Ubuntu 20.04+ has this by default. |

**Unsupported Environments**:
- Windows (native), macOS (native)
- CPU-only Linux (no NVIDIA GPU)
- Linux server environments (no GUI)
- ARM/RISC-V architectures

## Installation Requirements

Recommended to use Ubuntu or WSL2.

Install basic packages:

```bash
sudo apt update
sudo apt install -y \
  build-essential g++ make pkg-config \
  libuhd-dev uhd-host \
  libboost-system-dev \
  qt6-base-dev qt6-base-dev-tools
```

If using Qt5:

```bash
sudo apt install -y qtbase5-dev
```

If compiling CUDA files, also install CUDA Toolkit and verify:

```bash
nvcc --version
```

## Source Code Compilation

In the source code project root directory, run:

```bash
make clean
make -j$(nproc)
```

For frequent recompilation, consider installing ccache:

```bash
sudo apt install -y ccache
```

This project's Makefile will auto-detect `ccache`; after installation just use `make -j$(nproc)`.

After completion, the executable will be generated:

```bash
./bds-sim
```

## Portable Quick Start

To create a portable release package, in the source code directory run:

```bash
make portable
```

Or use a single command to do "parallel compilation + portable package generation":

```bash
make release
```

Version modes can be switched with `VERSION_MODE`:

- `manual`: Use `PORTABLE_VERSION` (default)
- `auto-patch`: Auto-increment patch from latest git tag `x.y.z` or `vx.y.z`
- `date`: Use date-based version (default format `YYYY.MM.DD`)

Common examples:

```bash
# Manual version (default)
make release PORTABLE_VERSION=1.2.3

# Auto-increment patch from git tag
make release VERSION_MODE=auto-patch

# Date version, e.g., 2026.04.06
make release VERSION_MODE=date
```

To preview the version first:

```bash
make print-portable-version VERSION_MODE=auto-patch
```

Important note: `make portable` is only for source code packages, not for running portable packages themselves.

Portable package usage:

1. Extract the portable package
2. Enter the extracted directory
3. Execute `./bds-sim` directly

If you see `dist/GNSS-SDRSIM-1.0-portable/`, it's already a packaged release directory; don't run `make portable` again.

Directory naming pattern:

`dist/{PORTABLE_NAME}-{effective-version}-portable`

The "effective version" is determined by `VERSION_MODE` and synced to `VERSION.txt` inside.

Portable version typically includes:

- Compiled executables
- Required data directories (e.g., `BRDM`, `fonts`, `ne_50m_land`)
- Required shared libraries
- Startup and check scripts

## Portable Package Dependencies

### Packaged Content (Auto-Included)

These dependencies are automatically packed into the portable package; **users don't need to install manually**:

| Dependency | Type | Source | Packaged |
|------------|------|--------|----------|
| **Qt Runtime** | Shared Library | Qt6 or Qt5 | ✅ Yes |
| **OpenGL Libraries** | Shared Library | Display Driver | ✅ Yes |
| **CUDA Runtime** | Shared Library | NVIDIA CUDA | ✅ Yes |
| **UHD Libraries** | Shared Library | UHD Driver | ✅ Yes (if included at compile time) |
| **libboost** | Shared Library | Boost | ✅ Yes |
| **libc/libm** | System Library | glibc | ⚠️ System-provided |

### Items That Require System Provision

These items **cannot be packaged** and must be provided by the target Linux distribution:

#### 1. **X11 Display Server** (or Wayland)
- **Why it can't be packaged**: X11 is a system-level graphics service, not an application library. It requires:
  - Direct hardware access (display, keyboard, mouse)
  - Kernel-level permissions
  - Auto-execution at system startup

- **How to check**:
  ```bash
  echo $DISPLAY          # Ubuntu Desktop: :0 or :1
  command -v Xvfb      # Check for virtual X Server
  ```

- **Solution for headless environments**:
  ```bash
  # Install virtual X Server
  sudo apt install xvfb
  
  # Start virtual display (1024x768 resolution)
  Xvfb :99 -screen 0 1024x768 &
  
  # Set environment variable
  export DISPLAY=:99
  
  # Now you can run
  ./bds-sim
  ```

#### 2. **NVIDIA Driver** (System-Level)
- **Why it can't be packaged**: Drivers need kernel interaction

- **Installation method**:
  ```bash
  # Ubuntu auto-driver install
  sudo ubuntu-drivers autoinstall
  
  # Or specify version manually
  sudo apt install nvidia-driver-550
  
  # Verify
  nvidia-smi
  ```

#### 3. **NVIDIA CUDA Runtime System Libraries**
- The portable package **will pack** CUDA libraries, but might be incompatible if system glibc is too old

- **Upgrade system C library** (if needed):
  ```bash
  sudo apt update && sudo apt upgrade
  ```

### Portable Package Installation Workflow

Recommended workflow for new systems:

```bash
# 1. Extract portable package
cd GNSS-SDRSIM-1.0-portable/

# 2. Check system environment (auto-detect missing items)
./scripts/portable-self-check.sh

# 3. If missing items, auto-install dependencies
./scripts/portable-oneclick-install.sh

# 4. Check again
./scripts/portable-self-check.sh

# 5. Run program
./bds-sim
```

Output results:
- `[ok]`: Environment complete, ready to run
- `[warn]`: Missing some optional items
- `[fail]`: Missing critical items, cannot run

## Complete Workflow

### Source Code Package

1. Install dependencies
2. In source root directory, run `make -j$(nproc)`
3. After compilation, execute `./bds-sim`

### Portable Package

1. In source root directory, run `make release` (recommended) or `make portable`
2. Get `dist/GNSS-SDRSIM-1.0-portable/`
3. Compress and distribute to users
4. Users extract and run `./bds-sim` directly

## After Startup

You'll see a full-screen GUI.

Basic 5-step workflow:

1. Click on the left map: Select starting point
2. Check right-bottom Control Panel: Select mode, adjust parameters
3. Press START: Begin simulation
4. During execution for path walking: Left-click to preview, right-click to confirm
5. To stop, press STOP SIMULATION

## Built-in GUI Tutorial

Top-right corner has a button:

- `GUIDE OFF`: Tutorial currently closed
- Toggle to `GUIDE ON` to enable step-by-step guidance

Tutorial shows you:

- Where to look
- Which button to press
- What to do next

You can press:

- `PREV`: Previous step
- `NEXT`: Next step
- `DONE`: Complete tutorial
- `CLOSE`: Close immediately

Key points:

- Tutorial won't auto-popup by default
- Only appears when you press `GUIDE`

## Ephemeris Files

Ephemeris files are in `./BRDM`.
Filename format:

- `BRDC00WRD_S_YYYYDDDHH00_01H_MN.rnx`

If no ephemeris available, the program will alert you; standard mode may not work.

## FAQ

**Q: Cannot find `nvcc`.**
A: CUDA Toolkit incomplete or PATH not set correctly.

**Q: Cannot find Qt during compilation.**
A: Install Qt development package and verify `pkg-config` finds Qt.

**Q: USRP initialization fails.**
A: Use `uhd_find_devices` to check hardware connection. Can still view GUI workflow without USRP.

**Q: Can portable package run on CPU-only computers?**
A: **No**. Program requires NVIDIA GPU. Without GPU, you might try source package's simulation mode (limited features).

**Q: Can it run on ARM or RISC-V architecture?**
A: **No**. Compiled binaries are x86-64 specific, and CUDA only supports x86/ARM64 specific models.

**Q: Can portable package run on Windows or macOS?**
A: **No**. Program is Linux-only. On Windows you can use WSL2 + Ubuntu. macOS not fully supported.

**Q: GPU not found, what to do?**
A:
  1. Check if nvidia-smi detects it: `nvidia-smi`
  2. If no output, may need reinstall/update NVIDIA driver
  3. Verify PCIe GPU properly inserted and powered
  4. Some laptops may need NVIDIA GPU via `prime` switcher

**Q: Which NVIDIA GPUs are supported?**
A: **Pascal (P40) or newer**:
  - Pascal (P40, Titan X)
  - Turing (RTX 2060, 2070, 2080, Quadro T)
  - Ampere (RTX 3060, 3070, 3090)
  - Ada (RTX 4070, 4090)
  - Blackwell (latest models)

Program auto-selects compatible version. Very old GPUs (Kepler/Maxwell) show error.

**Q: Why isn't X11 packaged in portable? Can it run on servers with no GUI?**
A:
  - X11 is an OS-level graphics service, not an app library; can't be packaged
  - Needs direct hardware access and independent process; can't copy like Qt
  - **Linux desktop with display**: X11 runs automatically, works directly
  - **Headless server**: Needs manual Xvfb (Virtual FrameBuffer) install:

    ```bash
    sudo apt install xvfb
    Xvfb :99 -screen 0 1024x768 &
    export DISPLAY=:99
    ./bds-sim
    ```

**Q: Error "can't connect to display server"?**
A:
  - Usually means no graphics environment running
  - For completely headless (server), install virtual X Server (see above)
  - If you have desktop, check `echo $DISPLAY` has a value

**Q: Who should use portable package?**
A:
  - End users (only want to run, not compile)
  - Those unfamiliar with Linux/compilation
  - Multi-computer distribution scenarios

Prerequisite: **target system meets system requirements** (Linux + NVIDIA GPU + GUI).

**Q: Is portable package size large?**
A: Usually **200-500 MB** (includes CUDA/Qt libs). Exact size depends on compile-time optimization.

**Q: Can I customize portable package content?**
A: Can modify `scripts/build-portable.sh`. But recommend keeping:
  - All GPU binary versions (auto-selects best)
  - Geographic data (`ne_50m_land/`)
  - Ephemeris data (`BRDM/`)
  - Self-check and install scripts

## Self-Check

- Can run `make -j`
- Can open `./bds-sim`
- Can click map to set start point
- Can press START
- Can press STOP
- Can toggle `GUIDE OFF/ON`

## Advanced Check Tools

To verify what's missing on a new computer:

```bash
./scripts/portable-self-check.sh
```

If missing, self-check will ask if you want one-click install. Or run directly:

```bash
./scripts/portable-oneclick-install.sh
```

After pressing `Y` in self-check, script auto-reruns check and outputs summary:

- `[result] Ready`: All good
- `[result] Still missing`: Some items missing

Note: Portable package still needs compatible Linux, NVIDIA driver; if using USRP, also needs hardware or UHD environment.

## GPU Architecture Support & Auto-Selection

Program compiles optimized binaries for multiple NVIDIA GPU architectures, auto-selecting compatible version at runtime.

### Supported GPU Architectures

| Architecture | Product GPU | Status | Binary | Compile Condition |
|-------------|------------|--------|--------|------------------|
| **Pascal (p61)** | P40, Quadro GP100 | ✅ Supported | bds-sim-pascal | HAS_SM_61 = true |
| **Turing (p75)** | RTX 2060-2080, Quadro T | ✅ Supported | bds-sim-turing | HAS_SM_75 = true |
| **Ampere (p86)** | RTX 3060-3090, A100 | ✅ Supported | bds-sim-ampere | HAS_SM_86 = true |
| **Ada (p89)** | RTX 4070-4090 | ⚠️ Partial | bds-sim-ada | HAS_SM_89 = true |
| **Blackwell (p100)** | GB200 etc. | ⚠️ Experimental | bds-sim-blackwell | GENCODE_BLACKWELL |
| **Kepler, Maxwell** | GTX 750-1080 | ❌ Not Supported | — | — |

### Auto-Selection Mechanism

At startup, launcher script:

1. Runs `nvidia-smi --query-gpu=compute_cap` to detect GPU compute capability
2. Compares with list of available binaries from compile time
3. Executes most compatible version

**Example**:
```bash
# System has RTX 3080 (Ampere, compute_cap = 8.6)
$ ./bds-sim
  → Detects compute_cap=86
  → Selects bin/bds-sim-ampere
  → Executes bds-sim-ampere
```

If no exact match, uses **fat binary** (bds-sim-fat) with multi-arch PTX code; slightly lower performance but better compatibility.

### Compile-Time Architecture Specification

To recompile with specific architectures:

```bash
# Compile only Ampere (RTX 30 series)
make clean && make HAS_SM_61=0 HAS_SM_75=0 HAS_SM_86=1 HAS_SM_89=0

# Compile latest Ampere + Ada (RTX 30 + RTX 40 series)
make clean && make HAS_SM_86=1 HAS_SM_89=1

# View Makefile cuda_targets.mk for all options
cat cuda/cuda_targets.mk
```

### Verify Compiled Architectures

```bash
# View all versions in portable package
ls -lh dist/GNSS-SDRSIM-*/bin/

# Check specific binary architectures
file bin/bds-sim-ampere
```
