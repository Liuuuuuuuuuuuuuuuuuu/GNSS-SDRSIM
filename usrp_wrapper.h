#ifndef USRP_WRAPPER_H
#define USRP_WRAPPER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USRP_SAMPLE_FMT_SHORT 0
#define USRP_SAMPLE_FMT_BYTE  1

/* * 初始化 USRP 設備
 * 回傳值: 0 表示成功，-1 表示失敗
 */
int usrp_init(double freq, double rate, double gain, int use_external_clk, int sample_format);

/* * 發送 I/Q 訊號到 USRP
 * iq_data: 交錯的 I/Q 陣列 (I0, Q0, I1, Q1...)
 * num_samples: 樣本數 (每組 I+Q 算一個樣本)
 * 回傳值: 實際發送的樣本數
 */
size_t usrp_send(const int16_t *iq_data, size_t num_samples);

/* * 安全關閉 USRP 串流
 */
void usrp_close(void);

/* Schedule USRP to start transmitting in given seconds from now (device time offset).
 * seconds_from_now may be negative (start immediately) or positive.
 */
void usrp_schedule_start_in(double seconds_from_now);

#ifdef __cplusplus
}
#endif

#endif /* USRP_WRAPPER_H */