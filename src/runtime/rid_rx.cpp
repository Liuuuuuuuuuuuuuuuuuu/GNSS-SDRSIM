/*
 * rid_rx.cpp  —  Remote ID 接收器 (BT LE + OpenDroneID + AoA)
 *
 * 實作摘要
 * ─────────────────────────────────────────────────────────────
 * 1. UHD 雙通道 RX: Chain A RX2 (ch0) + Chain B RX2 (ch1) @ 2.426 GHz, 4 MSPS
 *    與 GNSS TX (Chain A TX/RX @ 1.5 GHz) 同時運行。
 *    AD9361 TX LO 與 RX LO 相互獨立，無頻率衝突。
 *
 * 2. GFSK 解調 (BT 4 Legacy, 1 Mbps)
 *    FM discriminator: arg(z[n]·conj(z[n-1]))
 *    4× 過取樣 → 每個符號對 4 個 disc 樣本求和後做二元判決。
 *
 * 3. BT LE 封包同步
 *    40 bit 滑動移位暫存器，比對 Preamble (0x55, LSB-first =
 *    1,0,1,0,1,0,1,0) + Access Address (0x8E89BED6, LSB-first by byte)。
 *
 * 4. PDU 去白化 + CRC-24 驗證
 *    BT LE 白化 LFSR: G(D) = D^7 + D^4 + 1, seed = channel index (38)
 *    CRC-24: G(x) = x^24 + x^10 + x^9 + x^6 + x^4 + x^3 + x + 1,
 *    init = 0x555555。
 *
 * 5. OpenDroneID 解碼 (ASTM F3411-22a)
 *    AD type = 0x16 (Service Data), UUID = 0xFFFA。
 *    Message type 0 (Basic ID): UAS ID / UA type。
 *    Message type 1 (Location):  lat/lon/alt/speed/heading。
 *
 * 6. AoA 相位差估算
 *    Δφ = arg(Σ x_A[n]·conj(x_B[n])), 在 Preamble 32 個 IQ 樣本上求和。
 *    θ  = arcsin(Δφ·λ / (2π·d))，λ = c/2.426 GHz ≈ 0.1237 m。
 *    bearing_deg = θ + g_aoa_cal_offset_deg (可由校準程序更新)。
 *
 * 7. 路徑損耗距離估算 (粗略)
 *    d_m = 10^((P_tx - rssi_dBm - 40) / 20)，P_tx = 0 dBm (BT LE)。
 *
 * 8. UDP JSON → 127.0.0.1:39001 (DjiDetectManager)
 */

#include "rid_rx.h"
#include "main_gui.h"
#include "gui/nfz/dji_detect.h"

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/exception.hpp>

#include <atomic>
#include <complex>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ─────────────────────────────────────────────────────────────────
 * Forward declaration: usrp_wrapper.cpp 中的 static uhd::usrp_dev
 * ───────────────────────────────────────────────────────────────── */
uhd::usrp::multi_usrp::sptr usrp_get_dev(void);

/* ─────────────────────────────────────────────────────────────────
 * 常數
 * ───────────────────────────────────────────────────────────────── */
static const double RID_RATE_HZ    = 4e6;       /* 每符號 4 個樣本 @ 1 Mbps */
static const int    SPSYM          = 4;          /* samples per BT symbol */
static const int    RID_UDP_PORT   = 39001;
static const double RID_RX_TIMEOUT = 0.1;        /* UHD recv timeout (s) */

/* BT LE */
static const uint32_t BT_AA        = 0x8E89BED6u;
static const uint32_t BT_CRC_INIT  = 0x555555u;
static const uint32_t BT_CRC_POLY  = 0xDA6000u;  /* right-shift CRC-24 */
static const int      BT_MAX_PDU   = 39;          /* max ADV PDU bytes */

/* BLE Advertising channels: 37=2402MHz, 38=2426MHz, 39=2480MHz */
static const int      RID_ADV_CHANS[3] = {37, 38, 39};
static const double   RID_ADV_FREQS_HZ[3] = {2402e6, 2426e6, 2480e6};
static const int      RID_HOP_INTERVAL_MS = 120;

/* 每個取樣塊大小、攜帶緩衝 */
static const int RECV_BLOCK        = 4096;
static const int CARRY_SAMPS       = 1600; /* > max_pkt_samples = 45*8*4 = 1440 */
static const int TOTAL_BUF         = RECV_BLOCK + CARRY_SAMPS;

/* ─────────────────────────────────────────────────────────────────
 * 模組狀態
 * ───────────────────────────────────────────────────────────────── */
static uhd::rx_streamer::sptr g_rx_stream;
static std::thread            g_rx_thread;
static std::atomic<bool>      g_rx_active{false};
static int                    g_udp_sock  = -1;
static double                 g_ant_d_m   = 0.0625; /* 天線間距 */
static double                 g_aoa_cal   = 0.0;    /* 校準偏移量 (度) */
static double                 g_rx_gain   = 30.0;
static bool                   g_dual_chan = false;   /* 有無 Chain B RX */
static void*                  g_dji_detect_mgr = nullptr; /* DjiDetectManager* for direct AoA injection */
static std::atomic<int>       g_adv_chan_idx{1}; /* default CH38 */
static int                    g_aoa_last_bucket = -1;
static int                    g_aoa_stable_count = 0;
static long long              g_aoa_last_emit_ms = 0;

static inline int current_adv_ch(void) {
    int idx = g_adv_chan_idx.load(std::memory_order_relaxed);
    if (idx < 0 || idx > 2) idx = 1;
    return RID_ADV_CHANS[idx];
}

static inline double current_center_hz(void) {
    int idx = g_adv_chan_idx.load(std::memory_order_relaxed);
    if (idx < 0 || idx > 2) idx = 1;
    return RID_ADV_FREQS_HZ[idx];
}

static inline double current_lambda_m(void) {
    return 3e8 / current_center_hz();
}

/* ─────────────────────────────────────────────────────────────────
 * BT LE 白化 LFSR
 * G(D) = D^7 + D^4 + 1, 右移, 初始值 = 1|channel[5:0]
 * ───────────────────────────────────────────────────────────────── */
static uint8_t g_lfsr;

static void lfsr_init(int channel) {
    g_lfsr = (uint8_t)(((channel & 0x3F) | 0x40) & 0x7F);
}

static uint8_t lfsr_next_bit(void) {
    uint8_t out = g_lfsr & 1u;
    uint8_t fb  = (g_lfsr & 1u) ^ ((g_lfsr >> 3) & 1u);
    g_lfsr      = (uint8_t)((g_lfsr >> 1) | (fb << 6));
    return out;
}

static uint8_t bt_dewhiten_byte(uint8_t in) {
    uint8_t out = 0;
    for (int b = 0; b < 8; b++)
        out |= (uint8_t)((((in >> b) & 1u) ^ lfsr_next_bit()) << b);
    return out;
}

/* ─────────────────────────────────────────────────────────────────
 * CRC-24 (BT LE, LSB-first processing)
 * ───────────────────────────────────────────────────────────────── */
static uint32_t bt_crc24(const uint8_t *data, int len) {
    uint32_t crc = BT_CRC_INIT;
    for (int i = 0; i < len; i++) {
        for (int b = 0; b < 8; b++) {
            uint32_t bit = (data[i] >> b) & 1u;
            uint32_t fb  = (crc & 1u) ^ bit;
            crc >>= 1;
            if (fb) crc ^= BT_CRC_POLY;
        }
    }
    return crc & 0xFFFFFFu;
}

/* ─────────────────────────────────────────────────────────────────
 * 40-bit 同步字 (Preamble + AA) 預計算
 * ───────────────────────────────────────────────────────────────── */
static uint64_t g_sync_word;
static const uint64_t SYNC_MASK = ((uint64_t)1 << 40) - 1;

static void compute_sync_word(void) {
    /* AA=0x8E89BED6, 以 LSB-first 傳送, 低位元組 0xD6 先送 */
    const uint8_t aa[4] = {0xD6u, 0xBEu, 0x89u, 0x8Eu};
    /* 因 AA 第一個傳送 bit = LSB of 0xD6 = 0, Preamble = 0x55 */
    const uint8_t preamble = 0x55u;

    uint64_t sw = 0;
    for (int b = 0; b < 8; b++)  /* Preamble LSB-first */
        sw |= (uint64_t)((preamble >> b) & 1u) << (39 - b);
    for (int by = 0; by < 4; by++)
        for (int b = 0; b < 8; b++)
            sw |= (uint64_t)((aa[by] >> b) & 1u) << (39 - 8 - by * 8 - b);

    g_sync_word = sw;
}

/* ─────────────────────────────────────────────────────────────────
 * OpenDroneID 解碼結果
 * ───────────────────────────────────────────────────────────────── */
struct OdidLocation {
    double lat_deg;
    double lon_deg;
    double alt_geo_m;
    double speed_h_mps;
    double heading_deg;
    bool   valid;
};

struct OdidBasicId {
    char    uas_id[21];
    uint8_t ua_type;
    bool    valid;
};

static bool odid_decode_location(const uint8_t *msg, int len,
                                  OdidLocation &out) {
    /* ASTM F3411-22a Location message layout (25 bytes total):
     * Byte  0   : (MessageType=1)<<4 | ProtoVersion
     * Byte  1   : Status[7:4] | Reserved[3] | HeightType[2] | EWDir[1] | SpeedMult[0]
     * Byte  2   : Direction (0-179 degrees, + EWDir bit)
     * Byte  3   : Speed horizontal (×0.25 or ×0.75 m/s)
     * Byte  4   : Speed vertical (signed, ×0.5 m/s)
     * Bytes 5-8 : Latitude  (int32, ×1e-7 deg, little-endian)
     * Bytes 9-12: Longitude (int32, ×1e-7 deg, little-endian)
     * Bytes13-14: Alt barometric (uint16, ×0.5 m, offset -1000)
     * Bytes15-16: Alt geodetic    (same encoding)
     * Bytes17-18: Height          (same encoding)
     * ...
     */
    if (len < 25) return false;
    if ((msg[0] >> 4) != 1) return false;  /* MessageType must be 1 */

    int32_t lat_raw, lon_raw;
    memcpy(&lat_raw, msg + 5, 4);
    memcpy(&lon_raw, msg + 9, 4);

    out.lat_deg     = lat_raw * 1e-7;
    out.lon_deg     = lon_raw * 1e-7;

    uint16_t alt_geo_raw;
    memcpy(&alt_geo_raw, msg + 15, 2);
    out.alt_geo_m   = alt_geo_raw * 0.5 - 1000.0;

    uint8_t speed_byte = msg[3];
    uint8_t speed_mult = msg[1] & 1u;
    out.speed_h_mps = speed_byte * (speed_mult ? 0.75 : 0.25);

    uint8_t dir_byte = msg[2];
    bool    ew_dir   = (msg[1] >> 1) & 1u;
    out.heading_deg  = dir_byte + (ew_dir ? 180.0 : 0.0);

    out.valid = (out.lat_deg >= -90.0 && out.lat_deg <= 90.0 &&
                 out.lon_deg >= -180.0 && out.lon_deg <= 180.0);
    return out.valid;
}

static bool odid_decode_basic_id(const uint8_t *msg, int len,
                                   OdidBasicId &out) {
    /* Byte 0: (MessageType=0)<<4 | ProtoVersion
     * Byte 1: IDType[7:4] | UAType[3:0]
     * Bytes 2-21: UAS ID (20 bytes, null-padded string)
     */
    if (len < 25) return false;
    if ((msg[0] >> 4) != 0) return false;

    out.ua_type = msg[1] & 0x0Fu;
    memcpy(out.uas_id, msg + 2, 20);
    out.uas_id[20] = '\0';
    out.valid = true;
    return true;
}

/* ─────────────────────────────────────────────────────────────────
 * UDP JSON 發送
 * ───────────────────────────────────────────────────────────────── */
static const char *ua_type_to_str(uint8_t t) {
    switch (t) {
        case 2: return "Helicopter/VTOL";
        case 1: return "Aeroplane";
        case 4: return "Hybrid";
        default: return "Unknown";
    }
}

static void emit_udp(const uint8_t mac[6],
                     const OdidLocation &loc,
                     const OdidBasicId  &bid,
                     double rssi_dbm,
                     double bearing_deg,
                     double distance_m,
                     bool   aoa_valid) {
    if (g_udp_sock < 0) return;

    char device_id[32];
    snprintf(device_id, sizeof(device_id),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const char *model = bid.valid ? bid.uas_id : "Unknown";
    const char *ua    = bid.valid ? ua_type_to_str(bid.ua_type) : "Unknown";
    double confidence = 0.75;

    /* 若 OpenDroneID Location 有效，信心度提升 */
    if (loc.valid) confidence = 0.92;

    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"detected\":true,"
        "\"confidence\":%.2f,"
        "\"source\":\"bt-le-rid\","
        "\"remote_id\":true,"
        "\"vendor\":\"%s\","
        "\"model\":\"%s\","
        "\"device_id\":\"%s\","
        "\"oui\":\"%02x:%02x:%02x\","
        "\"rssi_dbm\":%.1f,"
        "\"has_rssi\":true",
        confidence, ua, model, device_id,
        mac[0], mac[1], mac[2],
        rssi_dbm);

    if (aoa_valid) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
            ",\"bearing_deg\":%.1f,"
            "\"distance_m\":%.1f,"
            "\"has_bearing\":true,"
            "\"has_distance\":true",
            bearing_deg, distance_m);
    }

    /* 若解出 GPS 座標，附加進 JSON 供上層顯示 */
    if (loc.valid) {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n,
            ",\"odid_lat\":%.7f,"
            "\"odid_lon\":%.7f,"
            "\"odid_alt_geo\":%.1f,"
            "\"odid_speed\":%.1f,"
            "\"odid_heading\":%.1f",
            loc.lat_deg, loc.lon_deg, loc.alt_geo_m,
            loc.speed_h_mps, loc.heading_deg);
    }

    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "}");

    struct sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(RID_UDP_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(g_udp_sock, buf, (size_t)n, MSG_NOSIGNAL,
           (struct sockaddr *)&sa, sizeof(sa));
}

static long long wallclock_ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

/* ─────────────────────────────────────────────────────────────────
 * BT LE 封包解析核心
 * 輸入: disc[]         (FM disc 輸出, TOTAL_BUF 個浮點數)
 *       ch0[], ch1[]   (原始 IQ 樣本, 用於 AoA, 長度同 disc)
 *       n_valid        (disc 中有效樣本數)
 *       pkt_start_bit  (在 bit 串流中的 sync 偵測位置)
 *       bit_phase      (4 個相位之一: 0,1,2,3)
 * ───────────────────────────────────────────────────────────────── */
static void process_packet(const float *disc,
                            const std::complex<float> *ch0,
                            const std::complex<float> *ch1,
                            int sync_bit_idx,   /* 40-bit 同步字最後一 bit */
                            int phase) {
    /* ── 提取 PDU bits ──────────────────────────────────────────── */
    /* PDU 最大: header(2) + payload(37) + CRC(3) = 42 bytes */
    const int MAX_BITS = 42 * 8;
    uint8_t raw[42] = {};
    int     n_bits   = 0;
    int     base_samp = (sync_bit_idx + 1) * SPSYM + phase;

    for (int bi = 0; bi < MAX_BITS; bi++) {
        int s0 = base_samp + bi * SPSYM;
        if (s0 + SPSYM > TOTAL_BUF) break;
        float sum = 0;
        for (int k = 0; k < SPSYM; k++) sum += disc[s0 + k];
        int bit = (sum > 0.0f) ? 1 : 0;
        raw[bi / 8] |= (uint8_t)(bit << (bi % 8));
        n_bits++;
    }
    if (n_bits < 16) return;  /* 至少讀到 header */

    /* ── 去白化 ─────────────────────────────────────────────────── */
    lfsr_init(current_adv_ch());
    /* 去白化從 PDU header 開始，不含 preamble 和 AA */
    uint8_t pdu[42] = {};
    int pdu_bytes = n_bits / 8;
    for (int i = 0; i < pdu_bytes; i++)
        pdu[i] = bt_dewhiten_byte(raw[i]);

    /* ── PDU header 解析 ────────────────────────────────────────── */
    uint8_t pdu_type = pdu[0] & 0x0Fu;
    int     pdu_len  = pdu[1] & 0x3Fu;  /* 6 bits (BT4) or 8 bits (BT5) */

    /* 只處理 ADV_IND(0), ADV_NONCONN_IND(2), SCAN_RSP(4) */
    if (pdu_type != 0 && pdu_type != 2 && pdu_type != 4) return;
    if (pdu_len < 6 || pdu_len > 37) return;     /* ADV_A(6) + data */
    if ((int)(pdu_len + 2 + 3) > pdu_bytes) return; /* need header+payload+CRC */

    /* ── CRC 驗證 ───────────────────────────────────────────────── */
    uint32_t crc_computed = bt_crc24(pdu, 2 + pdu_len);
    uint32_t crc_received = (uint32_t)pdu[2 + pdu_len] |
                            ((uint32_t)pdu[2 + pdu_len + 1] << 8) |
                            ((uint32_t)pdu[2 + pdu_len + 2] << 16);
    if (crc_computed != crc_received) return;

    /* ── 提取廣播地址 (AdvA, 6 bytes after header, LSB first) ──── */
    /* pdu[2..7] = AdvA */
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = pdu[2 + i];

    /* ── 掃描 AD structures 尋找 OpenDroneID ───────────────────── */
    const uint8_t *adv_data     = pdu + 8;  /* header(2) + AdvA(6) */
    int            adv_data_len = pdu_len - 6;
    OdidLocation   loc{};
    OdidBasicId    bid{};

    int pos = 0;
    while (pos < adv_data_len) {
        int  ad_len  = adv_data[pos];
        if (ad_len == 0) break;
        if (pos + 1 + ad_len > adv_data_len) break;

        uint8_t ad_type = adv_data[pos + 1];

        /* ASTM OpenDroneID: Service Data (0x16) with UUID 0xFFFA */
        if (ad_type == 0x16u && ad_len >= 28) {
            uint16_t uuid = (uint16_t)(adv_data[pos + 2] |
                                        ((unsigned)adv_data[pos + 3] << 8));
            if (uuid == 0xFFFAu) {
                /* OpenDroneID ODID payload starts at pos+4, length = ad_len-3 */
                const uint8_t *odid = adv_data + pos + 4;
                int odid_len        = ad_len - 3;
                odid_decode_location(odid, odid_len, loc);
                odid_decode_basic_id(odid, odid_len, bid);
            }
        }
        pos += 1 + ad_len;
    }

    /* ── RSSI 估算 ──────────────────────────────────────────────── */
    int preamble_samp = (sync_bit_idx - 39) * SPSYM + phase;
    if (preamble_samp < 0) preamble_samp = 0;
    float energy = 0.0f;
    int   nsamps  = 0;
    for (int k = 0; k < 32 && preamble_samp + k < TOTAL_BUF; k++, nsamps++) {
        float re = ch0[preamble_samp + k].real();
        float im = ch0[preamble_samp + k].imag();
        energy += re * re + im * im;
    }
    float mean_pwr = (nsamps > 0) ? energy / nsamps : 1e-30f;
    /* 近似 dBFS → dBm (B210 @30 dB gain ≈ 0 dBFS = -10 dBm; 粗略 offset) */
    double rssi_dbfs = 10.0 * std::log10((double)mean_pwr + 1e-30);
    double rssi_dbm  = rssi_dbfs - g_rx_gain + 20.0;

    /* ── AoA 相位差 ─────────────────────────────────────────────── */
    bool   aoa_valid   = false;
    double bearing_deg = 0.0;
    if (g_dual_chan && preamble_samp >= 0) {
        std::complex<float> acc{0, 0};
        for (int k = 0; k < 32 && preamble_samp + k < TOTAL_BUF; k++)
            acc += ch0[preamble_samp + k] *
                   std::conj(ch1[preamble_samp + k]);

        double delta_phi = std::arg((std::complex<double>)acc);
        /* θ = arcsin(Δφ·λ / (2π·d)) */
        double sin_theta = delta_phi * current_lambda_m() /
                           (2.0 * M_PI * g_ant_d_m);
        sin_theta  = std::max(-1.0, std::min(1.0, sin_theta));
        bearing_deg = std::asin(sin_theta) * 180.0 / M_PI + g_aoa_cal;
        aoa_valid   = true;
    }

    /* ── 路徑損耗距離估算 ────────────────────────────────────────── */
    /* FSPL model: d = 10^((P_tx - rssi_dBm - 40) / 20), P_tx = 0 dBm */
    double distance_m = std::pow(10.0, (0.0 - rssi_dbm - 40.0) / 20.0);
    distance_m = std::max(1.0, std::min(5000.0, distance_m));

    /* ── UTC 時間標記 (僅用於 stderr 日誌) ─────────────────────── */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tv;
    gmtime_r(&ts.tv_sec, &tv);

    fprintf(stderr,
            "[rid_rx] %02d:%02d:%02d  MAC=%02x:%02x:%02x:%02x:%02x:%02x"
            "  RSSI=%.1fdBm",
            tv.tm_hour, tv.tm_min, tv.tm_sec,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
            rssi_dbm);
    if (loc.valid)
        fprintf(stderr, "  GPS=%.6f,%.6f  Alt=%.1fm  Hdg=%.0f°",
                loc.lat_deg, loc.lon_deg, loc.alt_geo_m, loc.heading_deg);
    if (aoa_valid)
        fprintf(stderr, "  AoA=%.1f°  Dist~%.0fm", bearing_deg, distance_m);
    if (bid.valid)
        fprintf(stderr, "  ID=%s  UA=%s", bid.uas_id,
                ua_type_to_str(bid.ua_type));
    fprintf(stderr, "\n");

    // Anonymous AoA fallback: if no OpenDroneID payload was decodable but AoA is strong/stable
    // within 50m, report a synthetic target to DjiDetectManager with confidence ~0.75.
    const bool decoded_any = (loc.valid || bid.valid);
    if (!decoded_any && aoa_valid && (distance_m <= 50.0) && (rssi_dbm >= -90.0)) {
        const int bucket = (int)(std::fmod(bearing_deg + 360.0, 360.0) / 10.0);  // 10° bucket
        if (bucket == g_aoa_last_bucket) {
            g_aoa_stable_count++;
        } else {
            g_aoa_last_bucket = bucket;
            g_aoa_stable_count = 1;
        }

        const long long now_ms = wallclock_ms_now();
        if (g_aoa_stable_count >= 3 && (now_ms - g_aoa_last_emit_ms) >= 600) {
            rid_rx_report_aoa_anon(bearing_deg, distance_m, rssi_dbm);
            g_aoa_last_emit_ms = now_ms;
        }
    } else {
        g_aoa_stable_count = 0;
        g_aoa_last_bucket = -1;
    }

    /* ── UDP 發送 ────────────────────────────────────────────────── */
    emit_udp(mac, loc, bid, rssi_dbm, bearing_deg, distance_m, aoa_valid);
}

/* ─────────────────────────────────────────────────────────────────
 * 背景接收執行緒
 * ───────────────────────────────────────────────────────────────── */
static void rx_worker(void) {
    /* 樣本緩衝區：ch0 主解碼，ch1 AoA；carry 段保留上一塊的尾部 */
    std::vector<std::complex<float>> ch0(TOTAL_BUF), ch1(TOTAL_BUF);
    std::vector<float>               disc(TOTAL_BUF, 0.0f);

    /* FM discriminator 前一樣本 */
    std::complex<float> prev0{1, 0}, prev1{1, 0};

    /* 40-bit 滑動移位暫存器 for each of 4 symbol phases */
    uint64_t sr[4] = {0, 0, 0, 0};
    /* 每個 phase: 最近 bit 的樣本起始位置 */
    int phase_samp[4] = {0, 0, 0, 0};
    int bit_count = 0;  /* 已累積的 bit 數 (不含 carry bit) */

    long long last_hop_ms = wallclock_ms_now();

    /* bit 解調時使用的緩衝 (滾動) */
    /* 每個新樣本塊之前先重置 phase_samp (因為 disc 陣列已搬移) */

    while (g_rx_active.load(std::memory_order_relaxed)) {
        /* Periodic hop among BLE adv channels 37/38/39 to reduce miss rate. */
        const long long now_ms = wallclock_ms_now();
        if ((now_ms - last_hop_ms) >= RID_HOP_INTERVAL_MS) {
            int next_idx = g_adv_chan_idx.load(std::memory_order_relaxed);
            next_idx = (next_idx + 1) % 3;
            g_adv_chan_idx.store(next_idx, std::memory_order_relaxed);
            const double hop_hz = RID_ADV_FREQS_HZ[next_idx];
            try {
                uhd::usrp::multi_usrp::sptr dev = usrp_get_dev();
                if (dev) {
                    dev->set_rx_freq(uhd::tune_request_t(hop_hz), 0);
                    if (g_dual_chan) dev->set_rx_freq(uhd::tune_request_t(hop_hz), 1);
                }
            } catch (...) {
                /* Keep decoding on current channel if retune fails. */
            }
            last_hop_ms = now_ms;
        }

        /* 1. 把前一塊的尾部搬到開頭 (carry) */
        std::copy(ch0.begin() + RECV_BLOCK, ch0.end(),   ch0.begin());
        std::copy(ch1.begin() + RECV_BLOCK, ch1.end(),   ch1.begin());
        std::copy(disc.begin() + RECV_BLOCK, disc.end(), disc.begin());
        /* 調整 phase_samp 以反映搬移 */
        for (int p = 0; p < 4; p++) {
            phase_samp[p] -= RECV_BLOCK;
            if (phase_samp[p] < 0) phase_samp[p] = 0;
        }

        /* 2. 接收新樣本到 carry 段後方 */
        uhd::rx_metadata_t md;
        size_t n_recv;
        if (g_dual_chan) {
            std::vector<std::complex<float> *> buffs = {
                ch0.data() + CARRY_SAMPS,
                ch1.data() + CARRY_SAMPS};
            n_recv = g_rx_stream->recv(buffs, RECV_BLOCK, md, RID_RX_TIMEOUT);
        } else {
            n_recv = g_rx_stream->recv(ch0.data() + CARRY_SAMPS,
                                       RECV_BLOCK, md, RID_RX_TIMEOUT);
            /* 清空 ch1，AoA 無法運作但解碼仍可 */
            std::fill(ch1.begin() + CARRY_SAMPS,
                      ch1.begin() + CARRY_SAMPS + (int)n_recv,
                      std::complex<float>{0, 0});
        }

        if (n_recv == 0) continue;
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
            md.error_code != uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            fprintf(stderr, "[rid_rx] UHD RX error: %s\n",
                    md.strerror().c_str());
            continue;
        }

        /* 3. FM disc: 只計算新到的部份(起始 CARRY_SAMPS) */
        for (int i = CARRY_SAMPS; i < CARRY_SAMPS + (int)n_recv; i++) {
            std::complex<float> z = ch0[i];
            float re =  z.real() * prev0.real() + z.imag() * prev0.imag();
            float im =  z.imag() * prev0.real() - z.real() * prev0.imag();
            disc[i]  = std::atan2(im, re);
            prev0    = z;
        }

        /* 4. 對 4 個 symbol phase 分別做 bit 解調 + 同步字搜索 */
        for (int p = 0; p < 4; p++) {
            /* 從 CARRY_SAMPS + p 起算，步長 SPSYM */
            int start = CARRY_SAMPS + p;
            /* phase_samp[p]: 此 phase 在新塊中的第一個 bit 樣本位置 */
            if (phase_samp[p] < start) phase_samp[p] = start;

            for (int s = phase_samp[p];
                 s + SPSYM <= CARRY_SAMPS + (int)n_recv;
                 s += SPSYM) {
                /* 對 SPSYM 個 disc 樣本求和 → bit 判決 */
                float sum = 0;
                for (int k = 0; k < SPSYM; k++) sum += disc[s + k];
                int bit = (sum > 0.0f) ? 1 : 0;

                /* 更新滑動移位暫存器 (bit 39=最舊, bit 0=最新) */
                sr[p] = ((sr[p] << 1) | (uint64_t)bit) & SYNC_MASK;

                /* 檢查 40-bit 同步字 */
                if (sr[p] == g_sync_word) {
                    /* s + SPSYM - 1 是同步字最後一個 bit 的末尾樣本 */
                    int sync_sample = s + SPSYM - 1;
                    int sync_bit    = (sync_sample - start) / SPSYM + 1;
                    process_packet(disc.data(), ch0.data(), ch1.data(),
                                   sync_bit + (CARRY_SAMPS - start) / SPSYM,
                                   p);
                }
            }
            phase_samp[p] = CARRY_SAMPS + (int)n_recv - SPSYM + 1;
            if (phase_samp[p] < CARRY_SAMPS + p)
                phase_samp[p] = CARRY_SAMPS + p;
        }
        bit_count += (int)n_recv / SPSYM;
    }

    /* 停止 UHD 串流 */
    if (g_rx_stream) {
        uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
        g_rx_stream->issue_stream_cmd(cmd);
    }
}

/* ─────────────────────────────────────────────────────────────────
 * 公開 API
 * ───────────────────────────────────────────────────────────────── */
extern "C" int rid_rx_start(double ant_spacing_m, double rx_gain) {
    if (g_rx_active.load()) return 0;  /* 已在運行 */

    compute_sync_word();

    uhd::usrp::multi_usrp::sptr dev = usrp_get_dev();
    if (!dev) {
        fprintf(stderr, "[rid_rx] usrp_dev 尚未初始化，請先呼叫 usrp_init()\n");
        return -1;
    }

    g_ant_d_m  = (ant_spacing_m > 0.005) ? ant_spacing_m : 0.0625;
    g_rx_gain  = rx_gain;
    g_aoa_cal  = 0.0;

    try {
        /* ── RX 子裝置設定：A:A = Chain A frontend, A:B = Chain B frontend ── */
        /* B210 只有一塊 daughterboard A，兩個 frontend 分別是 A:A 和 A:B   */
        uhd::usrp::subdev_spec_t spec("A:A A:B");
        dev->set_rx_subdev_spec(spec, 0);

        /* ── Channel 0 (Chain A RX2) ─────────────────────────── */
        dev->set_rx_rate(RID_RATE_HZ, 0);
        dev->set_rx_freq(uhd::tune_request_t(current_center_hz()), 0);
        dev->set_rx_gain(rx_gain, 0);
        dev->set_rx_antenna("RX2", 0);     /* 使用 RX2 port，不干擾 TX/RX port */

        /* ── Channel 1 (Chain B RX2)：雙通道 AoA ──────────────── */
        bool ch1_ok = true;
        try {
            dev->set_rx_rate(RID_RATE_HZ, 1);
            dev->set_rx_freq(uhd::tune_request_t(current_center_hz()), 1);
            dev->set_rx_gain(rx_gain, 1);
            dev->set_rx_antenna("RX2", 1);
        } catch (...) {
            fprintf(stderr, "[rid_rx] Chain B 設定失敗，退回單通道模式 (無 AoA)\n");
            ch1_ok = false;
        }
        g_dual_chan = ch1_ok;

        /* ── 建立 RX stream ─────────────────────────────────────── */
        uhd::stream_args_t args("fc32", "sc16");
        if (g_dual_chan)
            args.channels = {0, 1};
        else
            args.channels = {0};

        try {
            g_rx_stream = dev->get_rx_stream(args);

            /* ── 開始連續接收 ─────────────────────────────────────── */
            uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            if (g_dual_chan) {
                /* UHD 要求多通道 RX 用 time_spec 啟動，才能保證通道對齊 */
                cmd.stream_now = false;
                cmd.time_spec = dev->get_time_now() + uhd::time_spec_t(0.05);
            } else {
                cmd.stream_now = true;
            }
            g_rx_stream->issue_stream_cmd(cmd);
        } catch (const std::exception &e) {
            if (!g_dual_chan) {
                throw;
            }

            /* 某些 master clock (例如 52 MHz) 下，雙通道 RX 會被 UHD 拒絕。
             * 自動降級為單通道，至少保留 RID 解碼能力。 */
            fprintf(stderr,
                    "[rid_rx] 雙通道 RX 啟動失敗，降級單通道: %s\n",
                    e.what());
            g_dual_chan = false;

            uhd::stream_args_t args_single("fc32", "sc16");
            args_single.channels = {0};
            g_rx_stream = dev->get_rx_stream(args_single);

            uhd::stream_cmd_t cmd_single(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            cmd_single.stream_now = true;
            g_rx_stream->issue_stream_cmd(cmd_single);
        }

    } catch (const std::exception &e) {
        fprintf(stderr, "[rid_rx] RX 初始化失敗: %s\n", e.what());
        return -1;
    }

    /* ── UDP socket ─────────────────────────────────────────────── */
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock < 0) {
        fprintf(stderr, "[rid_rx] 無法建立 UDP socket\n");
        return -1;
    }

    /* ── 啟動背景執行緒 ─────────────────────────────────────────── */
    g_rx_active.store(true);
    g_rx_thread = std::thread(rx_worker);

    fprintf(stderr,
            "[rid_rx] 啟動完畢  freq=%.3f MHz  rate=%.1f MSPS"
            "  gain=%.0f dB  AoA=%s (d=%.3f m)\n",
            current_center_hz() / 1e6,
            RID_RATE_HZ / 1e6,
            rx_gain,
            g_dual_chan ? "雙通道" : "停用",
            g_ant_d_m);

    map_gui_push_alert(0, "__i18n__:rid_rx.active");
    return 0;
}

extern "C" void rid_rx_stop(void) {
    if (!g_rx_active.load()) return;
    g_rx_active.store(false);
    if (g_rx_thread.joinable()) g_rx_thread.join();
    g_rx_stream.reset();
    if (g_udp_sock >= 0) { close(g_udp_sock); g_udp_sock = -1; }
    fprintf(stderr, "[rid_rx] 已停止\n");
}

extern "C" int rid_rx_is_active(void) {
    return g_rx_active.load() ? 1 : 0;
}

extern "C" void rid_rx_set_dji_detect_manager(void* mgr_ptr) {
    g_dji_detect_mgr = mgr_ptr;
}

extern "C" void rid_rx_report_aoa_anon(double bearing_deg, double distance_m, double rssi_meas_dbm) {
    if (!g_dji_detect_mgr) {
        return;  // DjiDetectManager not set, silently return
    }

    // Calculate confidence based on RSSI: strong signal (-80 dBm) → 0.75, weak (-100 dBm) → 0.60
    double confidence = 0.60;
    if (rssi_meas_dbm > -80.0) {
        confidence = 0.75;
    } else if (rssi_meas_dbm > -90.0) {
        confidence = 0.70;
    } else if (rssi_meas_dbm > -100.0) {
        confidence = 0.65;
    }

    // Call DjiDetectManager::inject_aoa_anonymous_target()
    DjiDetectManager* mgr = static_cast<DjiDetectManager*>(g_dji_detect_mgr);
    mgr->inject_aoa_anonymous_target(bearing_deg, distance_m, confidence);
}
