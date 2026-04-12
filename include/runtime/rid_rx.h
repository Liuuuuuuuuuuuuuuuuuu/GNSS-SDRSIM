#ifndef RID_RX_H
#define RID_RX_H

/*
 * rid_rx.h  —  Remote ID 接收器 (BT LE + OpenDroneID + AoA)
 *
 * 硬體配置:
 *   B210 Chain A TX/RX ── GNSS TX 天線 (1.5/1.57 GHz, 現有)
 *   B210 Chain A RX2   ── BT 天線 #0 (2.426 GHz)  ─┐ 相位差 → AoA
 *   B210 Chain B RX2   ── BT 天線 #1 (2.426 GHz)  ─┘ 間距 = ant_spacing_m
 *
 * 使用流程:
 *   1. usrp_init() 成功後呼叫 rid_rx_start()
 *   2. usrp_close() 前呼叫 rid_rx_stop()
 *
 * 輸出: UDP JSON → 127.0.0.1:39001 (DjiDetectManager 消費)
 * 欄位: device_id, vendor, model, bearing_deg, distance_m, rssi_dbm,
 *        detected, confidence, source="bt-le-rid"
 *
 * 解碼支援:
 *   - BT 4 Legacy Advertising (ADV_NONCONN_IND)
 *   - OpenDroneID Location/Vector (ASTM F3411-22a, msg type 1)
 *   - OpenDroneID Basic ID           (msg type 0)
 *   - AoA 相位差 → 方位角 (需校準 ant_spacing_m 和陣列朝向)
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 啟動 Remote ID 接收器。
 * ant_spacing_m : 兩根接收天線的物理間距 (公尺)，
 *                 建議 = λ/2 ≈ 0.0625 m @ 2.4 GHz (無模糊 AoA ±90°)。
 * rx_gain       : UHD RX 增益 (dB, 0–76)。
 * 回傳 0 = 成功，-1 = usrp_dev 尚未初始化或 RX stream 開啟失敗。
 */
int rid_rx_start(double ant_spacing_m, double rx_gain);

/* 停止 Remote ID 接收器並等待執行緒結束 (阻塞最多 3 秒)。 */
void rid_rx_stop(void);

/* 是否正在運行中。 */
int rid_rx_is_active(void);

/*
 * 設定 DjiDetectManager 指標，以便 rid_rx 能直接注入匿名 AoA 目標。
 * 應在 DjiDetectManager 建立後、rid_rx_start() 前呼叫。
 * 參數為 nullptr 表示禁用直接注入。
 */
void rid_rx_set_dji_detect_manager(void* mgr_ptr);  // mgr_ptr = DjiDetectManager*

/*
 * 向 DjiDetectManager 報告匿名 AoA 目標。
 * 當 rid_rx.cpp AoA 檢測到強相位信號但無法解碼 OpenDroneID 時，
 * 可呼叫此函式向管理員注入一個匿名威脅目標。
 * 
 * bearing_deg       : 方位角 (0° = North, 90° = East)
 * distance_m        : 估計距離 (公尺)
 * rssi_meas_dbm     : RSSI 測量值 (dBm，用於評估信心度)
 * 
 * 信心度計算: confidence = 0.75 if rssi > -80 dBm, else 0.60
 */
void rid_rx_report_aoa_anon(double bearing_deg, double distance_m, double rssi_meas_dbm);

#ifdef __cplusplus
}
#endif

#endif /* RID_RX_H */
