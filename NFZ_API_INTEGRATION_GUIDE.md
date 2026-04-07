# GNSS-SDRSIM NFZ 全球禁飛區渲染 - API 集成指南

## 📋 項目現況

### ✅ 已完成
1. **NFZ 數據結構增強** (`gui/nfz/dji_nfz.h`)
   - 支持複雜多邊形（Outer Ring + Inner Rings/Holes）
   - 支持體育場形（Stadium）、蝴蝶結形（Bow-tie）、倒結婚蛋糕形（Inverted Wedding Cake）
   - 支持 Circle 和 Polygon 兩種類型

2. **NFZ 渲染引擎升級** (`gui/nfz/dji_nfz.cpp`)
   - 使用 `Qt::OddEvenFill` 正確渲染帶洞多邊形
   - 自動解析外環 + 內環
   - 支持多種 API 欄位名稱：`inner_rings`, `holes`, `inner_polygons`

3. **碰撞檢測更新** (`gui/nfz/nfz_hit_test_utils.cpp`)
   - 射線法點在多邊形檢測
   - 支持洞的判定：**點在外環內 AND 不在任何洞內**

4. **試驗數據** (`test_nfz_sample.json`)
   - 台灣 3 個機場的標準格式樣本
   - 包含外環、內環、不同 Level 的範例

5. **全編譯驗證**
   - 所有 4 個 GPU 架構成功編譯
   - 無錯誤、無警告

---

## 🔗 DJI API 數據採集步驟

### 方法：瀏覽器開發者工具攔截

#### 步驟 1：打開 DJI 地圖
```
https://www.dji.com/tw/flysafe/geo-map
```

#### 步驟 2：開啟開發者工具
- **Windows/Linux**: F12
- **Mac**: Cmd + Option + I

#### 步驟 3：切換到 Network 標籤
- 在 Network 面板中選擇「XHR」或「Fetch」篩選
- 刷新頁面或在地圖上選擇國家/地區

#### 步驟 4：尋找 API 請求
典型的 DJI API 請求特徵：
- **URL 包含**: `/api/`, `geo`, `zone`, `flysafe`
- **Method**: GET 或 POST
- **Response**: JSON 格式，包含 `data.areas` 陣列

常見 endpoint 示例：
```
/api/v2/web/flysafe/zone/query
/api/geo/point
/api/restrictions
```

#### 步驟 5：複製完整 Response JSON
1. 點擊 API 請求
2. 找到 **Response** 標籤
3. 複製全部內容（Ctrl+A, Ctrl+C）
4. 貼到 `test_nfz_sample.json` 對應位置

---

## 📊 預期 API 回應格式

### 完整結構（支持所有機場類型）
```json
{
  "data": {
    "areas": [
      {
        "name": "機場名稱",
        "level": 2,
        "type": "airport",
        "polygon_points": [
          {"lat": 25.0900, "lng": 121.2200},
          ...
        ],
        "inner_rings": [
          [
            {"lat": 25.0850, "lng": 121.2300},
            ...
          ]
        ]
      }
    ]
  }
}
```

### 欄位說明

| 欄位 | 類型 | 說明 |
|------|------|------|
| `name` | string | 禁飛區名稱（機場名、政府機構等） |
| `level` | int | 禁航級別：1=授權區 (藍), 2=禁飛區(紅), 8=特殊禁飛 |
| `type` | string | 類型：airport, government, power_station, heliport 等 |
| `polygon_points` | array | 外環頂點：`[{lat, lng}, ...]` |
| `inner_rings` | array | 內環（洞）：`[[{lat, lng}, ...], [{lat, lng}, ...]]` |
| `holes` | array | 同 inner_rings（備用欄位） |
| `center_lat` | float | 圓形中心緯度 |
| `center_lon` | float | 圓形中心經度 |
| `radius` | float | 圓形半徑（公尺） |

---

## 🧪 測試驗證流程

### 1️⃣ 本地驗證（推薦）
```bash
# 驗證 JSON 格式
cd /home/user/GNSS-SDRSIM
python3 -c "
import json
with open('test_nfz_sample.json') as f:
    data = json.load(f)
    print(f'Found {len(data[\"data\"][\"areas\"])} areas')
"

# 編譯
make clean && make -j4
```

### 2️⃣ 運行應用程序
```bash
./bds-sim
```

### 3️⃣ 在 GUI 上驗證
- 地圖應顯示 NFZ 區域（紅色 = 禁飛，藍色 = 授權）
- 複雜形狀應正確渲染
- 洞（Inner Rings）應在外環內顯示為空白

---

## 🌍 全球支持規劃

### 階段 1：台灣機場（現成數據）
- ✅ 桃園國際機場（TAO）
- ✅ 高雄國際機場（KHH）
- ✅ 台北松山機場（TSA）
- 待驗證：其他小型機場

### 階段 2：亞洲主要機場
- 規劃：日本（NRT, HND, KIX）
- 規劃：新加坡（SIN）
- 規劃：香港（HKG）

### 階段 3：全球擴展
- 規劃：歐洲主要樞紐
- 規劃：北美、中東、澳洲

---

## 🔧 常見問題排查

### Q：如何區分外環和內環？
**A**：
- `polygon_points` = 外環（必須）
- `inner_rings`, `holes`, `inner_polygons` = 內環（可選）

### Q：Circle 和 Polygon 同時出現？
**A**：代碼優先使用 Polygon。Circle 用於直升機停機坪或簡單圓形區域。

### Q：如何驗證渲染正確性？
**A**：
1. 啟用 NFZ 顯示按鈕
2. 拖曳地圖確認位置
3. 檢查「洞」是否反 fillable（內環應為透明）
4. 驗證顏色：紅色(Level 2) vs 藍色(Level 1)

### Q：巨大的 API 回應（>100MB）？
**A**：代碼已有保護：
- 超過 2000 個禁航區時自動清空並重新開始
- 去重複邏輯防止重複添加

---

## 📝 下一步動作

1. **立即執行**：
   - 打開瀏覽器進入 DJI 地圖
   - 攔截 API 請求，複製完整 JSON
   - 貼入 `test_nfz_sample.json`

2. **測試驗證**：
   ```bash
   make clean && make -j4
   ./bds-sim
   ```

3. **提交反饋**：
   - 記錄渲染效果（形狀、顏色、性能）
   - 報告任何異常（錯誤位置、缺失區域等）

---

## 📚 技術參考

### 支持的 NFZ 形狀
| 形狀名稱 | 外環 | 內環 | 用途 |
|---------|------|------|------|
| 簡單圓形 | ❌ | ❌ | 直升機停機坪 |
| 簡單多邊形 | ✅ | ❌ | 基礎禁飛區 |
| 體育場形 | ✅ | ❌ | 機場跑道保護 |
| 蝴蝶結形 | ✅ | ❌ | 進出場航道 |
| 帶洞多邊形 | ✅ | ✅ | 倒結婚蛋糕形（多層限高） |

### 相關代碼檔案
- [gui/nfz/dji_nfz.h](gui/nfz/dji_nfz.h) - 數據結構
- [gui/nfz/dji_nfz.cpp](gui/nfz/dji_nfz.cpp) - API 解析 + 渲染
- [gui/nfz/nfz_hit_test_utils.cpp](gui/nfz/nfz_hit_test_utils.cpp) - 碰撞檢測
- [test_nfz_sample.json](test_nfz_sample.json) - 測試數據

---

**最後更新**：2026-04-07  
**聯絡人**：GitHub Copilot  
**版本**：1.0 (NFZ Architecture Complete)
