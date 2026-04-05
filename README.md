# GNSS-SDRSIM 新手教學文件

這份文件用最簡單的方式說明。
你可以把它當成「第一次玩這個程式」的操作卡。

## 0. 先知道這是什麼

這個程式可以做 GNSS 訊號模擬。
畫面會有地圖、控制面板、和訊號監看視窗。

## 1. 安裝需要的東西

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

## 2. 編譯

在專案目錄執行：

```bash
make clean
make -j
```

建議改用 CPU 核心數自動平行：

```bash
make clean
make -j$(nproc)
```

如果你常常重編，建議開啟 ccache：

```bash
sudo apt install -y ccache
```

本專案的 Makefile 會自動偵測 `ccache`，安裝後直接使用 `make -j$(nproc)` 即可。

完成後會有：

```bash
./bds-sim
```

## 3. 啟動

```bash
./bds-sim
```

打開後會看到全螢幕 GUI。

## 4. 最簡單 5 步驟（小白版）

1. 在左邊地圖按一下：選起點。
2. 看右下 Control Panel：選模式、調參數。
3. 按 START：開始跑。
4. 執行中如果要走路徑：左鍵預覽、右鍵確認。
5. 要停就按 STOP SIMULATION。

## 5. GUI 內建新手教學（預設關閉）

地圖右上有一個按鈕：

- `GUIDE OFF`：現在是關閉教學。
- 按一下後會變 `GUIDE ON`，並開啟一步一步教學。

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

- 預設不會自動彈教學。
- 只有你按 GUIDE 才會出現。

## 6. 星曆檔（一般模式會用到）

星曆放在 `./BRDM`。
檔名像這樣：

- `BRDC00WRD_S_YYYYDDDHH00_01H_MN.rnx`

如果沒有可用星曆，程式會提醒你，一般模式可能不能跑。

## 7. 常見問題

Q: `nvcc` 找不到。
A: 代表 CUDA Toolkit 還沒裝好，或 PATH 沒設好。

Q: 編譯時找不到 Qt。
A: 請安裝 Qt 開發套件，並確認 `pkg-config` 找得到 Qt。

Q: USRP 初始化失敗。
A: 先用 `uhd_find_devices` 檢查硬體連線。沒有 USRP 也可以先看 GUI 流程。

## 8. 自我檢查

- 可以 `make -j`。
- 可以開 `./bds-sim`。
- 可以在地圖點起點。
- 可以按 START。
- 可以按 STOP。
- 可以切換 `GUIDE OFF/ON`。
