### 🌍 Language / 語言

**繁體中文** | [English](README.md)

---

# GNSS-SDRSIM 發佈與使用說明

這份文件分成兩種使用情境：

- 原始碼包：適合要自行編譯、修改或檢視程式碼的人。
- portable 包：適合只想解壓後直接執行的人。

如果你只是要快速使用，先看 [portable 快速開始](#portable-快速開始)。

## 快速導覽

- [我該下載哪一種包](#我該下載哪一種包)
- [系統級需求](#系統級需求)
- [安裝需求](#安裝需求)
- [原始碼包編譯](#原始碼包編譯)
- [portable 快速開始](#portable-快速開始)
- [Portable 包的依賴關係](#portable-包的依賴關係)
- [完整操作流程](#完整操作流程)
- [常見問題](#常見問題)

## 我該下載哪一種包

### 原始碼包

原始碼包包含完整專案檔案，適合：

- 要自行編譯的人
- 要修改功能的人
- 要閱讀原始碼的人

你拿到後需要先安裝相依套件，再執行 `make`，完成後才能執行 `./bds-sim`。

### portable 包

portable 包是已經編譯完成的發佈版本，適合：

- 只想直接執行的人
- 不想處理編譯環境的人
- 要把程式交付給其他使用者的人

你拿到後通常只要解壓縮，進入目錄後直接執行 `./bds-sim`。

portable 包通常不會附上完整原始碼，因此對方一般看不到你的 `.c`、`.cpp`、`.cu`、`.h` 檔案。

## 系統級需求

本程式對執行環境有特定要求，無法跨平台執行。

### 硬體需求

| 項目 | 要求 | 說明 |
|------|------|------|
| **GPU** | NVIDIA 獨立顯卡 | 必須。Intel/AMD GPU 暫不支援。 |
| **GPU 架構** | Pascal (P40) 或更新 | 支援 Pascal、Turing、Ampere、Ada、Blackwell。自動選擇相容版本。 |
| **VRAM** | 至少 2 GB | 取決於模擬場景複雜度。 |

### 作業系統需求

| 項目 | 要求 | 說明 |
|------|------|------|
| **OS** | Linux | Ubuntu 20.04 LTS / 22.04 LTS 推薦。WSL2 (Ubuntu) 也支援。 |
| **NVIDIA Driver** | 已安裝 | 必須有相容於 CUDA 11.x/12.x 的驅動程式。 |
| **X11/Wayland** | 系統內建 | 桌面環境必須有圖形伺服器。無顯示器環境需用 Xvfb。 |
| **glibc** | 2.31+ | Ubuntu 20.04+ 預設有。 |

**無法執行的環境**：
- Windows (原生)、macOS (原生)
- CPU-only Linux (無 NVIDIA GPU)
- Linux 伺服器環境 (無圖形界面)
- ARM/RISC-V 架構

## 安裝需求

建議在 Ubuntu 或 WSL2 使用。

安裝基本套件：

```bash
sudo apt update
sudo apt install -y \
  build-essential g++ make pkg-config \
  libuhd-dev uhd-host \
  libboost-system-dev \
  qt6-base-dev qt6-base-dev-tools
```

如果你用 Qt5：

```bash
sudo apt install -y qtbase5-dev
```

如果你要編譯 CUDA 檔案，還需要安裝 CUDA Toolkit，並確認：

```bash
nvcc --version
```

## 原始碼包編譯

在原始碼專案根目錄執行：

```bash
make clean
make -j$(nproc)
```

如果你常常重編，建議安裝 ccache：

```bash
sudo apt install -y ccache
```

本專案的 Makefile 會自動偵測 `ccache`，安裝後直接使用 `make -j$(nproc)` 即可。

完成後會產生可執行檔：

```bash
./bds-sim
```

## portable 快速開始

如果你要做成可對外發佈的 portable 版本，請在「原始碼包」目錄執行：

```bash
make portable
```

或直接用一條命令同時完成「平行編譯 + 產生 portable 包」：

```bash
make release
```

版本號模式可用 `VERSION_MODE` 切換：

- `manual`：使用 `PORTABLE_VERSION`（預設）。
- `auto-patch`：從 git tag 取最新 `x.y.z` 或 `vx.y.z`，自動 +1 patch。
- `date`：使用日期版號（預設格式 `YYYY.MM.DD`）。

常用範例：

```bash
# 手動版本（預設）
make release PORTABLE_VERSION=1.2.3

# 自動從 tag 版號 +1 patch
make release VERSION_MODE=auto-patch

# 日期版號，例如 2026.04.06
make release VERSION_MODE=date
```

需要先預覽這次會用哪個版本號，可執行：

```bash
make print-portable-version VERSION_MODE=auto-patch
```

這一點要特別注意：`make portable` 只給原始碼包用，不是拿 portable 包本身來跑。

portable 包的使用方式是：

1. 解壓縮 portable 包。
2. 進入解壓後的目錄。
3. 直接執行 `./bds-sim`。

如果你看到的是 `dist/GNSS-SDRSIM-1.0-portable/`，那就是已經打包好的發佈目錄，不需要再執行 `make portable`。

目錄命名規則是：

`dist/{PORTABLE_NAME}-{有效版本號}-portable`

其中「有效版本號」由 `VERSION_MODE` 決定，並會同步寫入 portable 內的 `VERSION.txt`。

portable 版本通常會包含：

- 已編譯完成的執行檔
- 必要的資料目錄，例如 `BRDM`、`fonts`、`ne_50m_land`
- 需要的共享函式庫
- 啟動與檢查用腳本

## Portable 包的依賴關係

### 打包內容（自動包含）

下列依賴已經自動打包到 portable 包，**使用者不需手動安裝**：

| 依賴項 | 類型 | 來源 | 是否打包 |
|--------|------|------|--------|
| **Qt Runtime** | 共享庫 | Qt6 或 Qt5 | ✅ 打包 |
| **OpenGL Libraries** | 共享庫 | 顯示驅動 | ✅ 打包 |
| **CUDA Runtime** | 共享庫 | NVIDIA CUDA | ✅ 打包 |
| **UHD Libraries** | 共享庫 | UHD Driver | ✅ 打包 (若編譯時包含) |
| **libboost** | 共享庫 | Boost | ✅ 打包 |
| **libc/libm** | 系統庫 | glibc | ⚠️ 系統提供 |

### 需要系統提供的項目

下列項目**無法打包**，需要目標電腦的 Linux 發行版提供：

#### 1. **X11 Display Server** (或 Wayland)
- **為什麼無法打包**：X11 是系統級的圖形服務，不是應用程式庫。它需要：
  - 直接訪問硬體（顯示器、鍵盤、滑鼠）
  - 內核級權限
  - 系統啟動時自動執行
  
- **如何檢查**：
  ```bash
  echo $DISPLAY          # Ubuntu Desktop: :0 或 :1
  command -v Xvfb      # 有或無虛擬 X Server
  ```

- **無 GUI 環境下解決方法**：
  ```bash
  # 安裝虛擬 X Server
  sudo apt install xvfb
  
  # 啟動虛擬顯示（1024x768 解析度）
  Xvfb :99 -screen 0 1024x768 &
  
  # 設定環境變數
  export DISPLAY=:99
  
  # 現在可以執行
  ./bds-sim
  ```

#### 2. **NVIDIA Driver** (系統級)
- **為什麼無法打包**：驅動程式需要與核心交互
  
- **安裝方法**：
  ```bash
  # Ubuntu 自動驅動安裝
  sudo ubuntu-drivers autoinstall
  
  # 或手動指定版本
  sudo apt install nvidia-driver-550
  
  # 驗證
  nvidia-smi
  ```

#### 3. **NVIDIA CUDA Runtime 系統庫**
- portable 包**會打包** CUDA 庫，但如果系統 glibc 版本太舊，可能不相容
  
- **升級系統 C 庫**（若有問題）：
  ```bash
  sudo apt update && sudo apt upgrade
  ```

### Portable 包安裝流程

建議新電腦使用以下流程：

```bash
# 1. 解壓 Portable 包
cd GNSS-SDRSIM-1.0-portable/

# 2. 檢查系統環境 (自動檢查缺項)
./scripts/portable-self-check.sh

# 3. 若有缺項，自動安裝相依套件
./scripts/portable-oneclick-install.sh

# 4. 再檢查一次
./scripts/portable-self-check.sh

# 5. 執行程式
./bds-sim
```

輸出結果：
- `[ok] 可啟動`：環境完整，可以執行。
- `[warn] 缺少 X11`：無圖形環境，可考慮安裝 Xvfb 或圖形桌面。
- `[fail] 缺少 NVIDIA Driver`：必須安裝，否則無法執行。

## 完整操作流程

### 原始碼包

1. 安裝相依套件。
2. 在原始碼根目錄執行 `make -j$(nproc)`。
3. 編譯完成後執行 `./bds-sim`。

### portable 包

1. 在原始碼根目錄執行 `make release`（建議）或 `make portable`。
2. 取得 `dist/GNSS-SDRSIM-1.0-portable/`。
3. 將 portable 目錄壓縮後發給使用者。
4. 使用者解壓後直接執行 `./bds-sim`。

## 啟動後操作

啟動後會看到全螢幕 GUI。

最簡單 5 步驟：

1. 在左邊地圖按一下：選起點。
2. 看右下 Control Panel：選模式、調參數。
3. 按 START：開始跑。
4. 執行中如果要走路徑：左鍵預覽、右鍵確認。
5. 要停就按 STOP SIMULATION。

## GUI 內建新手教學

地圖右上有一個按鈕：

- `GUIDE OFF`：目前關閉教學。
- 按一下會變成 `GUIDE ON`，並開啟一步一步教學。

教學畫面會告訴你：

- 現在看哪裡
- 現在要按哪個按鈕
- 下一步做什麼

你可以按：

- `PREV`：回上一步
- `NEXT`：下一步
- `DONE`：完成教學
- `CLOSE`：立刻關閉教學

重點：

- 預設不會自動彈出教學。
- 只有你按 `GUIDE` 才會出現。

## 星曆檔

星曆放在 `./BRDM`。
檔名像這樣：

- `BRDC00WRD_S_YYYYDDDHH00_01H_MN.rnx`

如果沒有可用星曆，程式會提醒你，一般模式可能不能跑。

## 常見問題

Q: `nvcc` 找不到。
A: 代表 CUDA Toolkit 還沒安裝完成，或 PATH 沒設好。

Q: 編譯時找不到 Qt。
A: 請安裝 Qt 開發套件，並確認 `pkg-config` 找得到 Qt。

Q: USRP 初始化失敗。
A: 先用 `uhd_find_devices` 檢查硬體連線。沒有 USRP 也可以先看 GUI 流程。

Q: Portable 包可以在 CPU-only 的電腦上執行嗎？
A: **不行**。程式必須有 NVIDIA GPU。如果沒有 GPU 可同時試用原始碼包驅動的模擬模式（部分功能）。

Q: 可以在 ARM 或 RISC-V 架構執行嗎？
A: **不行**。編譯的二進位檔是 x86-64 特定的，且 CUDA 僅支援 x86/ARM64 的特定模型。

Q: Portable 包可以在 Windows 或 macOS 執行嗎？
A: **不行**。Program 是 Linux 專用。在 Windows 可用 WSL2 + Ubuntu 環境。macOS 目前無完整支援。

Q: GPU 顯卡找不到怎麼辦？
A: 
  1. 檢查 nvidia-smi 是否能偵測到: `nvidia-smi`
  2. 若無輸出，可能需要重新安裝或更新 NVIDIA 驅動
  3. 確認 PCIE 顯卡已正確插入且供電充足
  4. 某些筆記型電腦可能需要用 `prime` 切換到 NVIDIA GPU

Q: 支援哪些 NVIDIA GPU？
A: 支援 **Pascal (P40) 或更新的架構**：
  - Pascal (P40, Titan X)
  - Turing (RTX 2060, 2070, 2080, Quadro T)
  - Ampere (RTX 3060, 3070, 3090)
  - Ada (RTX 4070, 4090)
  - Blackwell (最新型號)
  
  程式會自動選擇相容版本。如果 GPU 過於老舊 (Kepler/Maxwell)，會顯示錯誤。

Q: 為什麼 Portable 包沒有自動打包 X11？可以在沒有圖形界面的伺服器執行嗎？
A: 
  - X11 不是應用程式庫，是作業系統級的圖形服務，無法打包。
  - 它需要直接存取硬體並運行獨立進程，不能像 Qt 庫一樣隨意複製。
  - **有顯示器的 Linux 桌面**：X11 自動執行，可直接運行。
  - **無顯示器的伺服器**：需手動安裝 Xvfb (Virtual FrameBuffer)：
  
    ```bash
    sudo apt install xvfb
    Xvfb :99 -screen 0 1024x768 &
    export DISPLAY=:99
    ./bds-sim
    ```

Q: 錯誤訊息 "can't connect to display server"？
A: 
  - 通常表示系統沒有執行圖形環境。
  - 若完全無 GUI（如伺服器環境），需安裝虛擬 X Server (見上題)。
  - 若有桌面環境，檢查 `echo $DISPLAY`，確認有值。

Q: Portable 包推薦給誰使用？
A: 
  - 終端使用者（只想執行，不想編譯）
  - 不熟悉 Linux/編譯的人
  - 要分發給多台電腦的場景
  
  前提是**目標電腦符合系統級需求**（Linux + NVIDIA GPU + 圖形界面）。

Q: Portable 包體積很大嗎？
A: 通常 **200-500 MB**（含 CUDA/Qt 庫）。具體大小取決於編譯時的最佳化選項。

Q: 能否自訂 Portable 包的內容？
A: 可以修改 `scripts/build-portable.sh` 自訂打包內容。但建議保留：
  - 所有 GPU 版本的二進位檔 (自動選擇最佳版本)
  - 地理資料 (`ne_50m_land/`)
  - 星曆資料 (`BRDM/`)
  - 自檢和安裝腳本

## 自我檢查

- 可以 `make -j`。
- 可以開 `./bds-sim`。
- 可以在地圖點起點。
- 可以按 START。
- 可以按 STOP。
- 可以切換 `GUIDE OFF/ON`。

## 進階檢查工具

如果你要先確認新電腦缺哪些條件，可以先跑：

```bash
./scripts/portable-self-check.sh
```

若有缺項，自檢腳本會在互動模式下詢問你是否要一鍵安裝常見相依套件。
你也可以手動直接執行：

```bash
./scripts/portable-oneclick-install.sh
```

當你在自檢詢問中按 `Y` 後，腳本會在安裝完成後自動再跑一次自檢，並在最後輸出單行結論：

- `[result] 可啟動`
- `[result] 仍缺少`

注意：這種免安裝包還是需要目標電腦有相容的 Linux、NVIDIA driver，若使用 USRP 也要有對應硬體或 UHD 環境。

## GPU 架構支援與自動選擇

本程式編譯時會對多種 NVIDIA GPU 架構生成最佳化二進位檔。執行時會自動選擇與 GPU 相容的版本。

### 支援的 GPU 架構

| 架構代號 | 產品 GPU | 支援狀態 | 二進位檔 | 編譯條件 |
|---------|---------|--------|--------|--------|
| **Pascal (p61)** | P40, Quadro GP100 | ✅ 支援 | bds-sim-pascal | HAS_SM_61 = true |
| **Turing (p75)** | RTX 2060-2080, Quadro T | ✅ 支援 | bds-sim-turing | HAS_SM_75 = true |
| **Ampere (p86)** | RTX 3060-3090, A100 | ✅ 支援 | bds-sim-ampere | HAS_SM_86 = true |
| **Ada (p89)** | RTX 4070-4090 | ⚠️ 部分支援 | bds-sim-ada | HAS_SM_89 = true |
| **Blackwell (p100)** | GB200 等最新型 | ⚠️ 試驗 | bds-sim-blackwell | GENCODE_BLACKWELL |
| **Kepler, Maxwell** | GTX 750-1080 | ❌ 不支援 | — | — |

### 自動選擇機制

啟動時，launcher 腳本會：

1. 執行 `nvidia-smi --query-gpu=compute_cap`，偵測 GPU 運算等級
2. 與編譯時已產生的二進位檔清單比對
3. 從最相容的版本執行

**例子**：
```bash
# 系統有 RTX 3080 (Ampere, compute_cap = 8.6)
$ ./bds-sim
  → 偵測到 compute_cap=86
  → 選擇 bin/bds-sim-ampere
  → 執行 bds-sim-ampere
```

若無完全相符的版本，會嘗試使用 **fat binary** (bds-sim-fat)，包含多架構 PTX 代碼，效能略低但相容性更好。

### 編譯時的架構指定

如果想重編並指定特定架構：

```bash
# 僅編譯 Ampere (RTX 30 系列)
make clean && make HAS_SM_61=0 HAS_SM_75=0 HAS_SM_86=1 HAS_SM_89=0

# 編譯最新的 Ampere + Ada (RTX 30 + RTX 40 系列)
make clean && make HAS_SM_86=1 HAS_SM_89=1

# 查看 Makefile 的 cuda_targets.mk 了解全部選項
cat cuda/cuda_targets.mk
```

### 驗證已編譯的架構

```bash
# 查看 portable 包包含的所有版本
ls -lh dist/GNSS-SDRSIM-*/bin/

# 檢查特定二進位檔支援的架構
file bin/bds-sim-ampere
```
