#ifndef GNSS_RX_H
#define GNSS_RX_H

/*
 * GUI 啟動時利用 USRP B210 RX 通道接收 GPS L1 C/A 信號，
 * 透過系統內建的 gnss-sdr 解算初始座標。
 *
 * 使用流程：
 *   1. start_map_gui() 之後呼叫 gnss_rx_start()
 *   2. 主迴圈中輪詢 gnss_rx_consume_fix()
 *   3. usrp_init() (TX 啟動) 之前必須呼叫 gnss_rx_cancel()
 */

/* 啟動背景定位執行緒 */
void gnss_rx_start(void);

/*
 * 若背景執行緒已取得有效定位，將結果寫入 lat_deg/lon_deg/h_m
 * 並回傳 1（一次性消費），否則回傳 0。
 */
int gnss_rx_consume_fix(double *lat_deg, double *lon_deg, double *h_m);

/*
 * 取消定位並等待 gnss-sdr 子行程完全結束（阻塞最多 5 秒）。
 * 必須在 usrp_init() 之前呼叫，確保 B210 已被釋放。
 */
void gnss_rx_cancel(void);

/* 是否仍在執行中 */
int gnss_rx_is_active(void);

#endif /* GNSS_RX_H */
