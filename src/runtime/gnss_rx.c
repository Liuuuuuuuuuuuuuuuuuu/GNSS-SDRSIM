/*
 * gnss_rx.c  —  GUI 啟動時背景 GPS 定位模組
 *
 * 流程：
 *   1. 寫臨時 gnss-sdr 設定檔到 /tmp
 *   2. fork/exec gnss-sdr，B210 進入 RX 模式
 *   3. 輪詢 NMEA 輸出檔，解析 $GPGGA 取得有效座標
 *   4. 取得定位後（或逾時/取消）終止 gnss-sdr 子行程釋放 B210
 */
#include "gnss_rx.h"
#include "main_gui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <glob.h>
#include <limits.h>
#include <termios.h>
#include <sys/select.h>

/* 超時時間（秒）：室內無法定位，30 秒後允許手動點擊地圖設定位置 */
#define GNSS_RX_TIMEOUT_SEC 30
/* 輪詢 NMEA 檔案間隔（毫秒） */
#define GNSS_RX_POLL_MS     500

/* ---------- 模組內部狀態 ---------- */
static pthread_t        g_rx_thread;
static volatile int     g_rx_thread_started = 0;
static volatile int     g_rx_cancel         = 0;
static volatile int     g_rx_active         = 0;

/* 定位結果（背景執行緒寫入，主迴圈讀取） */
static volatile int     g_fix_ready         = 0;
static volatile int     g_fix_consumed      = 0;
static double           g_fix_lat           = 0.0;
static double           g_fix_lon           = 0.0;
static double           g_fix_h             = 0.0;

/* gnss-sdr 子行程 PID */
static volatile pid_t   g_gnss_sdr_pid      = -1;

/* ---------- GNSS-SDR 設定檔範本 ----------
 * 使用 GPS L1 C/A，UHD_Signal_Source 自動偵測 B210 USB 裝置。
 * Channels=8 加速搜星；positioning_mode=Single 最快出解。
 * NMEA 輸出路徑由 %s 替換。
 */
static const char *GNSS_SDR_CONF_TMPL =
    "[GNSS-SDR]\n"
    "GNSS-SDR.internal_fs_sps=4000000\n"
    "\n"
    "SignalSource.implementation=UHD_Signal_Source\n"
    "SignalSource.item_type=gr_complex\n"
    "SignalSource.sampling_frequency=4000000\n"
    "SignalSource.freq=1575420000\n"
    "SignalSource.gain=60\n"
    "SignalSource.subdevice=A:A\n"
    "SignalSource.antenna=TX/RX\n"
    "SignalSource.samples=0\n"
    "SignalSource.repeat=false\n"
    "SignalSource.dump=false\n"
    "SignalSource.enable_throttle_control=false\n"
    "\n"
    "SignalConditioner.implementation=Pass_Through\n"
    "\n"
    "Channels_1C.count=8\n"
    "Channels.in_acquisition=1\n"
    "Channel.signal=1C\n"
    "\n"
    "Acquisition_1C.implementation=GPS_L1_CA_PCPS_Acquisition\n"
    "Acquisition_1C.item_type=gr_complex\n"
    "Acquisition_1C.coherent_integration_time_ms=1\n"
    "Acquisition_1C.threshold=0.008\n"
    "Acquisition_1C.doppler_max=10000\n"
    "Acquisition_1C.doppler_step=500\n"
    "Acquisition_1C.max_dwells=1\n"
    "Acquisition_1C.dump=false\n"
    "\n"
    "Tracking_1C.implementation=GPS_L1_CA_DLL_PLL_Tracking\n"
    "Tracking_1C.item_type=gr_complex\n"
    "Tracking_1C.pll_bw_hz=30.0\n"
    "Tracking_1C.dll_bw_hz=4.0\n"
    "Tracking_1C.order=3\n"
    "Tracking_1C.dump=false\n"
    "\n"
    "TelemetryDecoder_1C.implementation=GPS_L1_CA_Telemetry_Decoder\n"
    "TelemetryDecoder_1C.dump=false\n"
    "\n"
    "Observables.implementation=Hybrid_Observables\n"
    "Observables.dump=false\n"
    "\n"
    "PVT.implementation=RTKLIB_PVT\n"
    "PVT.positioning_mode=Single\n"
    "PVT.iono_model=Broadcast\n"
    "PVT.trop_model=Saastamoinen\n"
    "PVT.output_rate_ms=100\n"
    "PVT.display_rate_ms=500\n"
    "PVT.nmea_dump_filename=%s\n"
    "PVT.flag_nmea_tty_port=false\n"
    "PVT.flag_rtcm_server=false\n"
    "PVT.dump=false\n";

static int parse_gga(const char *line, double *lat, double *lon, double *h);

typedef struct serial_probe_result {
    int accessible;
    int saw_nmea;
    int saw_fix;
    int permission_denied;
    int baud;
    double lat;
    double lon;
    double h;
} serial_probe_result_t;

typedef struct candidate_device_set {
    char paths[16][PATH_MAX];
    size_t count;
} candidate_device_set_t;

static long long file_size_bytes(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

static int find_gnss_log_for_pid(pid_t pid, char *out_path, size_t out_path_sz)
{
    if (!out_path || out_path_sz == 0 || pid <= 0) return 0;

    DIR *dir = opendir("/tmp");
    if (!dir) return 0;

    char pid_token[32];
    snprintf(pid_token, sizeof(pid_token), ".%d", (int)pid);

    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "gnss-sdr.", 9) != 0) continue;
        if (strstr(entry->d_name, pid_token) == NULL) continue;
        snprintf(out_path, out_path_sz, "/tmp/%s", entry->d_name);
        found = 1;
        break;
    }

    closedir(dir);
    return found;
}

static void log_runtime_diag(const char *phase,
                             int elapsed_sec,
                             pid_t pid,
                             const char *conf_path,
                             const char *nmea_path)
{
    char log_path[PATH_MAX];
    log_path[0] = '\0';

    const int have_log = find_gnss_log_for_pid(pid, log_path, sizeof(log_path));
    const long long nmea_bytes = file_size_bytes(nmea_path);

    fprintf(stderr,
            "[gnss_rx] %s: elapsed=%ds pid=%d nmea=%lldB conf=%s nmea_file=%s",
            phase,
            elapsed_sec,
            (int)pid,
            nmea_bytes,
            conf_path ? conf_path : "(null)",
            nmea_path ? nmea_path : "(null)");
    if (have_log) {
        fprintf(stderr, " log=%s", log_path);
    }
    fputc('\n', stderr);
}

static speed_t baud_to_termios(int baud)
{
    switch (baud) {
        case 4800: return B4800;
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return 0;
    }
}

static int open_serial_nmea_device(const char *device_path, int baud)
{
    const speed_t speed = baud_to_termios(baud);
    if (speed == 0) {
        fprintf(stderr,
                "[gnss_rx] 不支援的 NMEA baud rate: %d\n",
                baud);
        return -1;
    }

    int fd = open(device_path, O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        if (errno == EACCES) {
            fprintf(stderr,
                    "[gnss_rx] 無法開啟外部 GPS 裝置 %s: %s (請確認使用者屬於 dialout 群組)\n",
                    device_path,
                    strerror(errno));
            map_gui_push_alert(2,
                "[GPS] 外部 GPS 已偵測到，但目前沒有 serial 權限；請將使用者加入 dialout 群組");
        } else {
            fprintf(stderr,
                    "[gnss_rx] 無法開啟外部 GPS 裝置 %s: %s\n",
                    device_path,
                    strerror(errno));
        }
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        fprintf(stderr,
                "[gnss_rx] 無法讀取序列埠設定 %s: %s\n",
                device_path,
                strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        fprintf(stderr,
                "[gnss_rx] 無法設定序列埠 %s: %s\n",
                device_path,
                strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIFLUSH);
    return fd;
}

static int candidate_device_seen(const candidate_device_set_t *set, const char *path)
{
    if (!set || !path) return 0;
    for (size_t i = 0; i < set->count; ++i) {
        if (strcmp(set->paths[i], path) == 0) return 1;
    }
    return 0;
}

static void candidate_device_add(candidate_device_set_t *set, const char *path)
{
    if (!set || !path || !path[0]) return;
    if (set->count >= (sizeof(set->paths) / sizeof(set->paths[0]))) return;
    if (candidate_device_seen(set, path)) return;
    snprintf(set->paths[set->count], sizeof(set->paths[set->count]), "%s", path);
    set->count++;
}

static serial_probe_result_t probe_serial_nmea_device(const char *device_path,
                                                      int baud,
                                                      int probe_ms)
{
    serial_probe_result_t result;
    memset(&result, 0, sizeof(result));
    result.baud = baud;

    int fd = open_serial_nmea_device(device_path, baud);
    if (fd < 0) {
        if (errno == EACCES) result.permission_denied = 1;
        return result;
    }

    result.accessible = 1;
    char line_buf[256];
    size_t line_len = 0;
    const int deadline_ms = probe_ms > 0 ? probe_ms : 1500;
    int elapsed_ms = 0;

    while (elapsed_ms < deadline_ms && !g_rx_cancel) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000;

        const int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        elapsed_ms += 200;
        if (ready == 0 || !FD_ISSET(fd, &readfds)) continue;

        char buf[128];
        ssize_t nread = read(fd, buf, sizeof(buf));
        if (nread <= 0) continue;

        for (ssize_t i = 0; i < nread; ++i) {
            const char ch = buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0) {
                    if (strncmp(line_buf, "$GP", 3) == 0 || strncmp(line_buf, "$GN", 3) == 0) {
                        result.saw_nmea = 1;
                    }

                    double lat, lon, h_m;
                    if (parse_gga(line_buf, &lat, &lon, &h_m)) {
                        result.saw_fix = 1;
                        result.lat = lat;
                        result.lon = lon;
                        result.h = h_m;
                        close(fd);
                        return result;
                    }
                }
                line_len = 0;
                continue;
            }

            if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = ch;
            } else {
                line_len = 0;
            }
        }
    }

    close(fd);
    return result;
}

static int auto_detect_serial_gps(char *out_device_path,
                                  size_t out_device_path_sz,
                                  int *out_baud)
{
    if (!out_device_path || out_device_path_sz == 0 || !out_baud) return 0;

    const char *patterns[] = {
        "/dev/serial/by-id/*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*"
    };
    const int baud_candidates[] = {4800, 9600};
    int saw_permission_denied = 0;
    int saw_accessible_candidate = 0;
    char fallback_device_path[PATH_MAX];
    int fallback_baud = 0;
    fallback_device_path[0] = '\0';
    candidate_device_set_t seen_devices;
    memset(&seen_devices, 0, sizeof(seen_devices));

    for (size_t pattern_index = 0; pattern_index < sizeof(patterns) / sizeof(patterns[0]); ++pattern_index) {
        glob_t matches;
        memset(&matches, 0, sizeof(matches));
        if (glob(patterns[pattern_index], 0, NULL, &matches) != 0) {
            globfree(&matches);
            continue;
        }

        for (size_t i = 0; i < matches.gl_pathc; ++i) {
            const char *candidate = matches.gl_pathv[i];
            char resolved_path[PATH_MAX];
            const char *effective_path = candidate;
            ssize_t resolved_len = readlink(candidate, resolved_path, sizeof(resolved_path) - 1);
            if (resolved_len > 0) {
                resolved_path[resolved_len] = '\0';
                if (resolved_path[0] != '/') {
                    char by_id_dir[PATH_MAX];
                    snprintf(by_id_dir, sizeof(by_id_dir), "%s", candidate);
                    char *slash = strrchr(by_id_dir, '/');
                    if (slash) {
                        *slash = '\0';
                        char joined[PATH_MAX];
                        snprintf(joined, sizeof(joined), "%.*s/%.*s",
                                 (int)(PATH_MAX / 2 - 2), by_id_dir,
                                 (int)(PATH_MAX / 2 - 2), resolved_path);
                        const char *rp = realpath(joined, resolved_path);
                        if (!rp) snprintf(resolved_path, sizeof(resolved_path), "%s", joined);
                    }
                }
                effective_path = resolved_path;
            }

            if (candidate_device_seen(&seen_devices, effective_path)) continue;
            candidate_device_add(&seen_devices, effective_path);

            for (size_t baud_index = 0; baud_index < sizeof(baud_candidates) / sizeof(baud_candidates[0]); ++baud_index) {
                const int baud = baud_candidates[baud_index];
                serial_probe_result_t probe = probe_serial_nmea_device(effective_path, baud, 6000);
                if (probe.permission_denied) saw_permission_denied = 1;
                if (!probe.accessible) continue;

                saw_accessible_candidate = 1;
                if (fallback_device_path[0] == '\0') {
                    snprintf(fallback_device_path, sizeof(fallback_device_path), "%s", effective_path);
                    fallback_baud = baud;
                }

                if (!probe.saw_nmea && !probe.saw_fix) continue;

                snprintf(out_device_path, out_device_path_sz, "%s", effective_path);
                *out_baud = baud;
                fprintf(stderr,
                        "[gnss_rx] 自動偵測到外部 GPS/NMEA: device=%s baud=%d nmea=%s fix=%s\n",
                        effective_path,
                        baud,
                        probe.saw_nmea ? "yes" : "no",
                        probe.saw_fix ? "yes" : "no");
                map_gui_push_alert(0,
                    "[GPS] 已自動偵測外部 GPS 接收器，優先使用外部定位");
                if (probe.saw_fix) {
                    g_fix_lat = probe.lat;
                    g_fix_lon = probe.lon;
                    g_fix_h = probe.h;
                    g_fix_ready = 1;
                }
                globfree(&matches);
                return 1;
            }
        }
        globfree(&matches);
    }

    if (fallback_device_path[0] != '\0' && saw_accessible_candidate && seen_devices.count == 1) {
        snprintf(out_device_path, out_device_path_sz, "%s", fallback_device_path);
        *out_baud = fallback_baud;
        fprintf(stderr,
                "[gnss_rx] 未在快速探測視窗內看到可用 fix，但已偵測到唯一外部 serial GPS；改為持續等待外部 GPS: device=%s baud=%d\n",
                fallback_device_path,
                fallback_baud);
        map_gui_push_alert(0,
            "[GPS] 已偵測外部 GPS，正在持續等待衛星定位...");
        return 1;
    }

    if (saw_permission_denied) {
        fprintf(stderr,
                "[gnss_rx] 偵測到可能的外部 GPS，但目前無法讀取 serial 裝置。請確認執行使用者屬於 dialout 群組。\n");
    }
    return 0;
}

static int try_fix_from_serial_nmea(const char *device_path, int baud)
{
    if (!device_path || !device_path[0]) return 0;

    int fd = open_serial_nmea_device(device_path, baud);
    if (fd < 0) return 0;

    fprintf(stderr,
            "[gnss_rx] 使用外部 GPS/NMEA: device=%s baud=%d\n",
            device_path,
            baud);
    map_gui_push_alert(0,
        "[GPS] 使用外部 GPS 接收器定位中...");

    time_t start_time = time(NULL);
    int last_progress_report_sec = -10;
    char line_buf[256];
    size_t line_len = 0;

    while (!g_rx_cancel) {
        const int elapsed = (int)(time(NULL) - start_time);
        if (elapsed >= last_progress_report_sec + 10) {
            last_progress_report_sec = elapsed;
            fprintf(stderr,
                    "[gnss_rx] 外部 GPS 搜星中: elapsed=%ds device=%s baud=%d\n",
                    elapsed,
                    device_path,
                    baud);
        }
        if (elapsed > GNSS_RX_TIMEOUT_SEC) {
            fprintf(stderr,
                    "[gnss_rx] 外部 GPS 定位逾時 (%d 秒): %s\n",
                    GNSS_RX_TIMEOUT_SEC,
                    device_path);
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = GNSS_RX_POLL_MS * 1000;

        int ready = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr,
                    "[gnss_rx] 讀取外部 GPS 失敗 %s: %s\n",
                    device_path,
                    strerror(errno));
            break;
        }
        if (ready == 0 || !FD_ISSET(fd, &readfds)) continue;

        char buf[128];
        ssize_t nread = read(fd, buf, sizeof(buf));
        if (nread < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            fprintf(stderr,
                    "[gnss_rx] 外部 GPS read 失敗 %s: %s\n",
                    device_path,
                    strerror(errno));
            break;
        }
        if (nread == 0) continue;

        for (ssize_t i = 0; i < nread; ++i) {
            const char ch = buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                line_buf[line_len] = '\0';
                if (line_len > 0) {
                    double lat, lon, h_m;
                    if (parse_gga(line_buf, &lat, &lon, &h_m)) {
                        g_fix_lat = lat;
                        g_fix_lon = lon;
                        g_fix_h = h_m;
                        g_fix_ready = 1;
                        fprintf(stderr,
                                "[gnss_rx] 外部 GPS 定位成功: %.6f°, %.6f°, %.1f m (%s)\n",
                                lat, lon, h_m, device_path);
                        map_gui_push_alert(0,
                            "[GPS] 外部 GPS 定位成功，已取得目前設備位置");
                        close(fd);
                        return 1;
                    }
                }
                line_len = 0;
                continue;
            }
            if (line_len + 1 < sizeof(line_buf)) {
                line_buf[line_len++] = ch;
            } else {
                line_len = 0;
            }
        }
    }

    close(fd);
    if (!g_rx_cancel) {
        fprintf(stderr, "[gnss_rx] 外部 GPS 定位超時。如在室內，請在地圖上左鍵點擊您當前位置。\n");
        map_gui_push_alert(1, "[GPS] 外部 GPS 未能定位。請在地圖上點擊您的當前位置");
    }
    return 0;
}

/* ---------- 輔助函式 ---------- */

/*
 * 解析 $GPGGA / $GNGGA NMEA 句。
 * 回傳 1 表示解出有效定位（quality >= 1）。
 */
static int parse_gga(const char *line, double *lat, double *lon, double *h)
{
    if (strncmp(line, "$GPGGA", 6) != 0 && strncmp(line, "$GNGGA", 6) != 0)
        return 0;

    /* 複製一份再 tokenize，避免修改原字串 */
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[16];
    int   nf = 0;
    char *p  = buf;
    while (nf < 15) {
        fields[nf++] = p;
        char *c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p  = c + 1;
    }
    if (nf < 10) return 0;

    int quality = atoi(fields[6]);
    if (quality < 1) return 0;

    /* 緯度：DDMM.MMMM → 十進位度 */
    double raw_lat = atof(fields[2]);
    double lat_d   = floor(raw_lat / 100.0) + fmod(raw_lat, 100.0) / 60.0;
    if (fields[3][0] == 'S') lat_d = -lat_d;

    /* 經度：DDDMM.MMMM → 十進位度 */
    double raw_lon = atof(fields[4]);
    double lon_d   = floor(raw_lon / 100.0) + fmod(raw_lon, 100.0) / 60.0;
    if (fields[5][0] == 'W') lon_d = -lon_d;

    double alt = atof(fields[9]);

    if (lat_d < -90.0 || lat_d > 90.0 || lon_d < -180.0 || lon_d > 180.0)
        return 0;

    *lat = lat_d;
    *lon = lon_d;
    *h   = alt;
    return 1;
}

/* ---------- 背景執行緒本體 ---------- */
static void *rx_thread_func(void *arg)
{
    (void)arg;
    g_rx_active = 1;

    const char *serial_device = getenv("GNSS_RX_NMEA_DEVICE");
    int serial_baud = 4800;
    const char *baud_env = getenv("GNSS_RX_NMEA_BAUD");
    if (baud_env && baud_env[0]) {
        const int parsed_baud = atoi(baud_env);
        if (parsed_baud > 0) serial_baud = parsed_baud;
    }

    if (serial_device && serial_device[0]) {
        const int got_serial_fix = try_fix_from_serial_nmea(serial_device, serial_baud);
        g_rx_active = 0;
        if (!got_serial_fix) {
            fprintf(stderr,
                    "[gnss_rx] 外部 GPS/NMEA 未取得定位，若要回退 gnss-sdr，請不要設定 GNSS_RX_NMEA_DEVICE\n");
        }
        return NULL;
    }

    char auto_serial_device[PATH_MAX];
    auto_serial_device[0] = '\0';
    if (auto_detect_serial_gps(auto_serial_device, sizeof(auto_serial_device), &serial_baud)) {
        if (g_fix_ready) {
            fprintf(stderr,
                    "[gnss_rx] 自動偵測外部 GPS 已直接提供定位: %.6f°, %.6f°, %.1f m (%s)\n",
                    g_fix_lat, g_fix_lon, g_fix_h, auto_serial_device);
            g_rx_active = 0;
            return NULL;
        }

        const int got_serial_fix = try_fix_from_serial_nmea(auto_serial_device, serial_baud);
        g_rx_active = 0;
        if (!got_serial_fix) {
            fprintf(stderr,
                    "[gnss_rx] 自動偵測到外部 GPS/NMEA，但尚未取得定位: %s baud=%d\n",
                    auto_serial_device,
                    serial_baud);
        }
        return NULL;
    }

    /* ── 步驟 1：建立臨時設定檔 ── */
    char conf_path[64] = "/tmp/bds-gnssrx-cfgXXXXXX";
    int  conf_fd       = mkstemp(conf_path);
    if (conf_fd < 0) {
        fprintf(stderr, "[gnss_rx] 無法建立設定檔: %s\n", strerror(errno));
        g_rx_active = 0;
        return NULL;
    }

    char nmea_path[64] = "/tmp/bds-gnssrx-nmeaXXXXXX";
    {
        int fd2 = mkstemp(nmea_path);
        if (fd2 < 0) {
            close(conf_fd);
            unlink(conf_path);
            fprintf(stderr, "[gnss_rx] 無法建立 NMEA 暫存檔: %s\n", strerror(errno));
            g_rx_active = 0;
            return NULL;
        }
        close(fd2);
    }

    char conf_content[3072];
    snprintf(conf_content, sizeof(conf_content), GNSS_SDR_CONF_TMPL, nmea_path);
    {
        ssize_t n = write(conf_fd, conf_content, strlen(conf_content));
        (void)n;
    }
    close(conf_fd);

    /* ── 步驟 2：fork + exec gnss-sdr ── */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[gnss_rx] fork 失敗: %s\n", strerror(errno));
        unlink(conf_path);
        unlink(nmea_path);
        g_rx_active = 0;
        return NULL;
    }

    if (pid == 0) {
        /* 子行程：把 stdout/stderr 導向 /dev/null，改到 /tmp 執行 */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        if (chdir("/tmp") != 0) { /* 忽略錯誤，讓 gnss-sdr 在 /tmp 產生暫存檔 */ }
        execl("/usr/bin/gnss-sdr", "gnss-sdr",
              "--config_file", conf_path,
              "--log_dir=/tmp",
              NULL);
        _exit(1);
    }

    g_gnss_sdr_pid = pid;
    fprintf(stderr, "[gnss_rx] GNSS-SDR 已啟動 (PID %d)，等待 GPS L1 定位...\n",
            (int)pid);
    log_runtime_diag("啟動參數", 0, pid, conf_path, nmea_path);
    map_gui_push_alert(0,
        "__i18n__:gnss_rx.searching");

    /* ── 步驟 3：輪詢 NMEA 檔案等待有效定位 ── */
    time_t start_time = time(NULL);
    FILE  *nmea_f     = NULL;
    char   line_buf[256];
    int    got_fix    = 0;
    int    last_progress_report_sec = -10;

    while (!g_rx_cancel) {
        time_t elapsed = time(NULL) - start_time;
        if ((int)elapsed >= last_progress_report_sec + 10) {
            last_progress_report_sec = (int)elapsed;
            log_runtime_diag("仍在搜星", (int)elapsed, pid, conf_path, nmea_path);
        }
        if (elapsed > GNSS_RX_TIMEOUT_SEC) {
            fprintf(stderr, "[gnss_rx] 定位逾時 (%d 秒)，放棄\n",
                    GNSS_RX_TIMEOUT_SEC);
            log_runtime_diag("逾時診斷", (int)elapsed, pid, conf_path, nmea_path);
            break;
        }

        /* 偵測子行程是否提前結束 */
        {
            int    status = 0;
            pid_t  waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                if (WIFEXITED(status)) {
                    fprintf(stderr, "[gnss_rx] GNSS-SDR 提前結束，exit code=%d\n",
                            WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    fprintf(stderr, "[gnss_rx] GNSS-SDR 提前結束，signal=%d\n",
                            WTERMSIG(status));
                } else {
                    fprintf(stderr, "[gnss_rx] GNSS-SDR 提前結束\n");
                }
                log_runtime_diag("提前結束診斷", (int)elapsed, pid, conf_path, nmea_path);
                g_gnss_sdr_pid = -1;
                break;
            }
        }

        /* 開啟或保持開啟 NMEA 檔案 */
        if (!nmea_f) {
            nmea_f = fopen(nmea_path, "r");
        }

        if (nmea_f) {
            while (fgets(line_buf, sizeof(line_buf), nmea_f)) {
                double lat, lon, h_m;
                if (parse_gga(line_buf, &lat, &lon, &h_m)) {
                    g_fix_lat     = lat;
                    g_fix_lon     = lon;
                    g_fix_h       = h_m;
                    g_fix_ready   = 1;
                    got_fix       = 1;
                    fprintf(stderr,
                            "[gnss_rx] GPS 定位成功: %.6f°, %.6f°, %.1f m\n",
                            lat, lon, h_m);
                    break;
                }
            }
        }

        if (got_fix) break;

        struct timespec ts = { .tv_sec = 0, .tv_nsec = GNSS_RX_POLL_MS * 1000000L };
        nanosleep(&ts, NULL);
    }

    /* ── 步驟 4：終止 gnss-sdr 子行程，釋放 B210 ── */
    if (g_gnss_sdr_pid > 0) {
        kill(g_gnss_sdr_pid, SIGTERM);
        /* 等最多 3 秒讓 gnss-sdr 優雅退出 */
        struct timespec ts2 = { .tv_sec = 3, .tv_nsec = 0 };
        nanosleep(&ts2, NULL);
        if (waitpid(g_gnss_sdr_pid, NULL, WNOHANG) == 0) {
            kill(g_gnss_sdr_pid, SIGKILL);
            waitpid(g_gnss_sdr_pid, NULL, 0);
        }
        g_gnss_sdr_pid = -1;
    }

    if (nmea_f) fclose(nmea_f);

    if (!got_fix && !g_rx_cancel) {
        fprintf(stderr,
                "[gnss_rx] 保留診斷檔案供檢查: conf=%s nmea=%s\n",
                conf_path, nmea_path);
        fprintf(stderr, "[gnss_rx] GPS 定位超時。使用座標 22.758423°, 120.337893° 作為預設位置\n");
        /* 設定 fallback 座標（用戶自訂的室內位置）並自動縮放 */
        g_fix_lat = 22.758423;
        g_fix_lon = 120.337893;
        g_fix_h = 0.0;
        g_fix_ready = 1;
        map_gui_push_alert(0, "[GPS] 已自動設定起始座標: 22.758423°, 120.337893° (室內預設)");
    } else {
        unlink(conf_path);
        unlink(nmea_path);
    }

    g_rx_active = 0;
    return NULL;
}

/* ---------- 公開 API ---------- */

void gnss_rx_start(void)
{
    if (g_rx_thread_started) return;
    g_rx_cancel   = 0;
    g_fix_ready   = 0;
    g_fix_consumed = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* joinable：讓 gnss_rx_cancel() 能確認執行緒已結束 */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&g_rx_thread, &attr, rx_thread_func, NULL) == 0) {
        g_rx_thread_started = 1;
    } else {
        fprintf(stderr, "[gnss_rx] 無法建立執行緒: %s\n", strerror(errno));
    }
    pthread_attr_destroy(&attr);
}

int gnss_rx_is_active(void)
{
    return g_rx_active;
}

int gnss_rx_consume_fix(double *lat_deg, double *lon_deg, double *h_m)
{
    if (!g_fix_ready || g_fix_consumed) return 0;
    *lat_deg     = g_fix_lat;
    *lon_deg     = g_fix_lon;
    *h_m         = g_fix_h;
    g_fix_consumed = 1;
    return 1;
}

void gnss_rx_cancel(void)
{
    if (!g_rx_thread_started) return;

    g_rx_cancel = 1;

    /* 先送 SIGTERM 給 gnss-sdr，讓它優雅關閉 */
    if (g_gnss_sdr_pid > 0) {
        kill((pid_t)g_gnss_sdr_pid, SIGTERM);
    }

    /* 等待背景執行緒結束（最多 5 秒） */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 5;
    int rc = pthread_timedjoin_np(g_rx_thread, NULL, &deadline);
    if (rc != 0) {
        /* 超時仍未結束：強制 SIGKILL */
        if (g_gnss_sdr_pid > 0) {
            kill((pid_t)g_gnss_sdr_pid, SIGKILL);
        }
        /* 再等一小段讓 OS 回收行程 */
        struct timespec ts3 = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts3, NULL);
    }

    g_rx_thread_started = 0;
}
