/* globals.c: global variables and PRN generation */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "globals.h"
#include "bdssim.h"
#include "rinex.h"

/* --------------------------- 全域資料區 ------------------------------*/
uint8_t      prn_code[MAX_SAT][CODE_LEN];   /* Gold 2046-chip */
uint8_t      gps_prn_code[GPS_EPH_SLOTS][GPS_CA_LEN]; /* GPS L1 C/A 1023-chip */
bds_ephemeris_t  eph[MAX_SAT];                  /* ★ 真正配置記憶體 ★ */
gps_ephemeris_t gps_eph[GPS_EPH_SLOTS];     /* GPS 星曆分開儲存 */

double nav_time_min = 0.0;
double nav_time_max = 0.0;
int    nav_week     = 0;
double iono_alpha[4] = {0};
double iono_beta[4]  = {0};
int    utc_bdt_diff  = 4;   /* default UTC->BDT offset */
int    utc_gpst_diff = 18;  /* default UTC->GPST offset */ 

int simulator_inited = 0;
int prn_max = 63;

#define NAV_MIN_SATS_PER_SYSTEM 8

/* Global transmit time (BDT seconds) */
double g_t_tx = 0.0;

/* Enable/disable ionospheric delay model */
int g_enable_iono = 1;
volatile int g_runtime_abort = 0;

/* PRN mask currently selected for active signal generation */
int g_active_prn_mask[MAX_SAT] = {0};

/* Receiver position for GUI display */
double g_receiver_lat_deg = 0.0;
double g_receiver_lon_deg = 0.0;
int g_receiver_valid = 0;
double g_bds_if_offset_hz = RF_MIXED_BDS_IF_OFFSET_HZ;
double g_gps_if_offset_hz = RF_MIXED_GPS_IF_OFFSET_HZ;

/* Shared spectrum buffer for GUI monitor */
float g_gui_spectrum_db[GUI_SPECTRUM_BINS] = {0.0f};
int g_gui_spectrum_bins = GUI_SPECTRUM_BINS;
int g_gui_spectrum_valid = 0;
uint64_t g_gui_spectrum_seq = 0;
pthread_mutex_t g_gui_spectrum_mtx = PTHREAD_MUTEX_INITIALIZER;
int16_t g_gui_time_iq[2 * GUI_TIME_MON_SAMPLES] = {0};
int g_gui_time_samples = GUI_TIME_MON_SAMPLES;
int g_gui_time_valid = 0;

/* --------------------------- BDS B1I PRN 產生 ------------------------------*/
#define ITER 2047                     /* 先跑滿 2047，再丟掉最後 1 chip */

/* 求 11-bit 內容在 mask 位置上的 XOR parity */
static inline uint8_t parity(uint16_t x)
{
    x ^= x >> 8;  x ^= x >> 4;  x ^= x >> 2;  x ^= x >> 1;
    return x & 1;
}

/* Fibonacci LFSR 單步：整串左移，回饋 bit 塞到 bit0 */
static inline uint16_t lfsr_step(uint16_t s, uint16_t mask)
{
    uint8_t fb = parity(s & mask);
    return ((s << 1) & 0x7FE) | fb;         /* 只保留 11 位 */
}

/* 生成指定 PRN (1–63) 的 2046-chip 代碼 */
static void cb1i_generate(int prn, uint8_t *dst)
{
    if (prn < 1 || prn > 63) return;

    uint16_t g1 = 0x2AA;   /* 01010101010b */
    uint16_t g2 = 0x2AA;
    const uint8_t *tap = g2_taps[prn];

    for (int i = 0; i < ITER; ++i) {
        uint8_t g1_out = (g1 >> 10) & 1;              /* stage 11 */
        uint8_t g2_out = ((g2 >> (tap[0]-1)) & 1) ^
                         ((g2 >> (tap[1]-1)) & 1) ^
                         (tap[2] ? ((g2 >> (tap[2]-1)) & 1) : 0);

        if (i < CODE_LEN) dst[i] = g1_out ^ g2_out;

        g1 = lfsr_step(g1, 0x7C1   /* 1,7,8,9,10,11 */);
        g2 = lfsr_step(g2, 0x59F   /* 1,2,3,4,5,8,9,11 */);
    }
}

/* --------------------------- GPS L1 C/A PRN 產生 --------------------------*/

/* 10-bit parity for GPS LFSR */
static inline uint8_t parity10(uint16_t x)
{
    x ^= x >> 8;  x ^= x >> 4;  x ^= x >> 2;  x ^= x >> 1;
    return x & 1;
}

/* 10-bit Fibonacci LFSR: shift left, feedback into bit0 */
static inline uint16_t lfsr_step10(uint16_t s, uint16_t mask)
{
    uint8_t fb = parity10(s & mask);
    return ((s << 1) & 0x3FF) | fb;
}

void gps_l1_ca_generate(int prn, uint8_t *dst)
{
    if (!dst) return;
    if (prn < 1 || prn > GPS_PRN_MAX) return;

    uint16_t g1 = 0x3FF;
    uint16_t g2 = 0x3FF;
    int s1 = gps_ca_taps[prn][0] - 1;
    int s2 = gps_ca_taps[prn][1] - 1;

    for (int i = 0; i < GPS_CA_LEN; ++i) {
        uint8_t g1_out = (g1 >> 9) & 1;
        uint8_t g2_out = ((g2 >> s1) & 1) ^ ((g2 >> s2) & 1);
        dst[i] = g1_out ^ g2_out;

        uint16_t g1_mask = (1u << 2) | (1u << 9);                    /* taps 3,10 */
        uint16_t g2_mask = (1u << 1) | (1u << 2) | (1u << 5) |
                           (1u << 7) | (1u << 8) | (1u << 9);         /* taps 2,3,6,8,9,10 */
        g1 = lfsr_step10(g1, g1_mask);
        g2 = lfsr_step10(g2, g2_mask);
    }
}

void init_prn_tables(void)
{
    for (int p = 1; p <= prn_max; ++p) {
        cb1i_generate(p, prn_code[p]);
    }
    for (int p = 1; p <= GPS_PRN_MAX; ++p) {
        gps_l1_ca_generate(p, gps_prn_code[p]);
    }
    printf("[bdssim] BDS PRN 表已產生 (1–%d)\n", prn_max);
    printf("[bdssim] GPS PRN 表已產生 (1–%d)\n", GPS_PRN_MAX);
}

static int count_loaded_bds_sats(void)
{
    int n = 0;
    for (int prn = 1; prn <= BDS_PRN_MAX; ++prn) {
        if (eph[prn].prn != 0) ++n;
    }
    return n;
}

static int count_loaded_gps_sats(void)
{
    int n = 0;
    for (int prn = 1; prn <= GPS_PRN_MAX; ++prn) {
        if (gps_eph[prn].prn != 0) ++n;
    }
    return n;
}

/* 對外 API */
bool init_simulator(sim_config_t *cfg, double start_bdt)
{
    if(simulator_inited) return true;
    prn_max = 63;
    init_prn_tables();
    memset(eph, 0, sizeof(eph));
    memset(gps_eph, 0, sizeof(gps_eph));
    memset(iono_alpha, 0, sizeof(iono_alpha));
    memset(iono_beta, 0, sizeof(iono_beta));
    if(read_rinex_nav(cfg->rinex_file_bds, cfg->rinex_file_gps, start_bdt)!=0) return false;

    int bds_count = count_loaded_bds_sats();
    int gps_count = count_loaded_gps_sats();

    if (bds_count < NAV_MIN_SATS_PER_SYSTEM) {
        fprintf(stderr,
                "[rinex][warn] 啟動載入 BDS 衛星數 %d < %d，BDS 參數不啟用\n",
                bds_count, NAV_MIN_SATS_PER_SYSTEM);
        memset(eph, 0, sizeof(eph));
    } else {
        nav_week = (int)(nav_time_min / 604800.0);
    }

    if (gps_count < NAV_MIN_SATS_PER_SYSTEM) {
        fprintf(stderr,
                "[rinex][warn] 啟動載入 GPS 衛星數 %d < %d，GPS 參數不啟用\n",
                gps_count, NAV_MIN_SATS_PER_SYSTEM);
        memset(gps_eph, 0, sizeof(gps_eph));
    }

    if (bds_count < NAV_MIN_SATS_PER_SYSTEM && gps_count < NAV_MIN_SATS_PER_SYSTEM) {
        fprintf(stderr,
                "[rinex][error] 啟動載入失敗：BDS/GPS 衛星數皆低於 %d\n",
                NAV_MIN_SATS_PER_SYSTEM);
        return false;
    }

    simulator_inited = 1;
    return true;
}

bool reload_simulator_nav(sim_config_t *cfg, double start_bdt)
{
    if (!cfg) return false;
    if (!simulator_inited) return init_simulator(cfg, start_bdt);

    bds_ephemeris_t old_bds[MAX_SAT];
    gps_ephemeris_t old_gps[GPS_EPH_SLOTS];
    double old_iono_alpha[4];
    double old_iono_beta[4];
    double old_nav_time_min = nav_time_min;
    double old_nav_time_max = nav_time_max;
    int old_nav_week = nav_week;

    memcpy(old_bds, eph, sizeof(old_bds));
    memcpy(old_gps, gps_eph, sizeof(old_gps));
    memcpy(old_iono_alpha, iono_alpha, sizeof(old_iono_alpha));
    memcpy(old_iono_beta, iono_beta, sizeof(old_iono_beta));

    memset(eph, 0, sizeof(eph));
    memset(gps_eph, 0, sizeof(gps_eph));
    memset(iono_alpha, 0, sizeof(iono_alpha));
    memset(iono_beta, 0, sizeof(iono_beta));
    char old_bds_path[256] = {0};
    char old_gps_path[256] = {0};
    snprintf(old_bds_path, sizeof(old_bds_path), "%s", cfg->rinex_file_bds);
    snprintf(old_gps_path, sizeof(old_gps_path), "%s", cfg->rinex_file_gps);

    read_rinex_nav(cfg->rinex_file_bds, cfg->rinex_file_gps, start_bdt);

    int bds_count = count_loaded_bds_sats();
    int gps_count = count_loaded_gps_sats();
    bool bds_ok = (bds_count >= NAV_MIN_SATS_PER_SYSTEM);
    bool gps_ok = (gps_count >= NAV_MIN_SATS_PER_SYSTEM);

    if (!bds_ok) {
        memcpy(eph, old_bds, sizeof(eph));
        memcpy(iono_alpha, old_iono_alpha, sizeof(iono_alpha));
        memcpy(iono_beta, old_iono_beta, sizeof(iono_beta));
        nav_time_min = old_nav_time_min;
        nav_time_max = old_nav_time_max;
        nav_week = old_nav_week;
        fprintf(stderr,
                "[rinex][warn] 重載 BDS 衛星數 %d < %d，保留舊 BDS 參數 (%s)\n",
                bds_count, NAV_MIN_SATS_PER_SYSTEM, old_bds_path);
        snprintf(cfg->rinex_file_bds, sizeof(cfg->rinex_file_bds), "%s", old_bds_path);
    } else {
        nav_week = (int)(nav_time_min / 604800.0);
    }

    if (!gps_ok) {
        memcpy(gps_eph, old_gps, sizeof(gps_eph));
        fprintf(stderr,
                "[rinex][warn] 重載 GPS 衛星數 %d < %d，保留舊 GPS 參數 (%s)\n",
                gps_count, NAV_MIN_SATS_PER_SYSTEM, old_gps_path);
        snprintf(cfg->rinex_file_gps, sizeof(cfg->rinex_file_gps), "%s", old_gps_path);
    }

    if (!bds_ok && !gps_ok) {
        fprintf(stderr,
                "[rinex][warn] 重載取消：BDS/GPS 衛星數皆低於 %d，維持舊參數\n",
                NAV_MIN_SATS_PER_SYSTEM);
        return false;
    }

    return true;
}

void cleanup_simulator(void){ simulator_inited = 0; }
/* ---------------------------  End  ------------------------------*/
