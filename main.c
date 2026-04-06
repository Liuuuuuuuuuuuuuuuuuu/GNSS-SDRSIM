#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <math.h>
#include <glob.h>
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>

#include "bdssim.h"
#include "timeconv.h"
#include "globals.h"
#include "path.h"
#include "channel.h"
#include "coord.h"
#include "usrp_wrapper.h"
#include "main_gui.h"
#include "cuda/cuda_runtime_info.h"

typedef struct {
    bool start;
    bool stop;
    bool help;
    bool quit;
    bool file_delete;
    bool has_path_file;
    char path_file[256];
} line_cmd_t;

typedef struct {
    char path_file[256];
} queued_path_t;

#define MAX_PATH_QUEUE 5
#define INPUT_QUEUE_CAP 32

typedef struct {
    char lines[INPUT_QUEUE_CAP][1024];
    int head;
    int tail;
    int count;
    pthread_mutex_t mtx;
} input_queue_t;

static input_queue_t g_input_q = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .mtx = PTHREAD_MUTEX_INITIALIZER
};
static volatile int g_input_stop = 0;
static volatile int g_input_eof = 0;
static volatile int g_runtime_running = 0;
static volatile int g_runtime_path_busy = 0;
static volatile int g_runtime_path_queue_count = 0;
static queued_path_t g_path_queue[MAX_PATH_QUEUE] = {0};
static int g_path_q_count = 0;
static pthread_mutex_t g_path_q_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_spoof_autocenter_done = 0;

static void print_pending_path_queue_locked(void)
{
    if (g_path_q_count <= 0) {
        puts("[run] 待執行路徑佇列: (空)");
        return;
    }
    printf("[run] 待執行路徑佇列[%d/%d]:", g_path_q_count, MAX_PATH_QUEUE);
    for (int i = 0; i < g_path_q_count; ++i) {
        printf(" {%d}%s", i + 1, g_path_queue[i].path_file);
    }
    printf("\n");
}

static void print_pending_path_queue(void)
{
    pthread_mutex_lock(&g_path_q_mtx);
    print_pending_path_queue_locked();
    pthread_mutex_unlock(&g_path_q_mtx);
}

int enqueue_path_file_name(const char *path)
{
    int ok = 0;
    pthread_mutex_lock(&g_path_q_mtx);
    if (g_path_q_count < MAX_PATH_QUEUE) {
        snprintf(g_path_queue[g_path_q_count].path_file,
                 sizeof(g_path_queue[g_path_q_count].path_file),
                 "%s", path);
        g_path_q_count += 1;
        g_runtime_path_queue_count = g_path_q_count;
        ok = 1;
    }
    pthread_mutex_unlock(&g_path_q_mtx);
    return ok;
}

int delete_last_queued_path(char *removed, size_t removed_sz)
{
    int ok = 0;
    pthread_mutex_lock(&g_path_q_mtx);
    if (g_path_q_count > 0) {
        if (removed && removed_sz > 0) {
            snprintf(removed, removed_sz, "%s", g_path_queue[g_path_q_count - 1].path_file);
        }
        g_path_q_count -= 1;
        g_runtime_path_queue_count = g_path_q_count;
        ok = 1;
    }
    pthread_mutex_unlock(&g_path_q_mtx);
    return ok;
}

static int pop_first_queued_path(queued_path_t *out)
{
    int ok = 0;
    pthread_mutex_lock(&g_path_q_mtx);
    if (g_path_q_count > 0) {
        *out = g_path_queue[0];
        for (int i = 1; i < g_path_q_count; ++i) g_path_queue[i - 1] = g_path_queue[i];
        g_path_q_count -= 1;
        g_runtime_path_queue_count = g_path_q_count;
        ok = 1;
    }
    pthread_mutex_unlock(&g_path_q_mtx);
    return ok;
}

static void clear_queued_paths(void)
{
    pthread_mutex_lock(&g_path_q_mtx);
    g_path_q_count = 0;
    g_runtime_path_queue_count = 0;
    pthread_mutex_unlock(&g_path_q_mtx);
}

static void cleanup_runtime_path_files(void)
{
    const char *dir = "./runtime_paths";
    DIR *dp = opendir(dir);
    if (!dp) return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        size_t n = strlen(name);
        if (strncmp(name, "seg_", 4) != 0) continue;
        if (n < 8 || strcmp(name + n - 4, ".llh") != 0) continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        unlink(path);
    }
    closedir(dp);

    rmdir(dir);
}

static int parse_llh_file_inline(const char *line, char out_path[256])
{
    const char *prefix = "--llh-file";
    size_t plen = strlen(prefix);
    if (strncmp(line, prefix, plen) != 0) return 0;

    const char *p = line + plen;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '=') p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return 0;

    snprintf(out_path, 256, "%.255s", p);
    return 1;
}

static int input_queue_push(const char *line)
{
    int ok = 0;
    pthread_mutex_lock(&g_input_q.mtx);
    if (g_input_q.count < INPUT_QUEUE_CAP) {
        snprintf(g_input_q.lines[g_input_q.tail], sizeof(g_input_q.lines[g_input_q.tail]), "%s", line);
        g_input_q.tail = (g_input_q.tail + 1) % INPUT_QUEUE_CAP;
        g_input_q.count++;
        ok = 1;
    }
    pthread_mutex_unlock(&g_input_q.mtx);
    return ok;
}

static int input_queue_pop(char *line, size_t sz)
{
    int ok = 0;
    pthread_mutex_lock(&g_input_q.mtx);
    if (g_input_q.count > 0) {
        snprintf(line, sz, "%s", g_input_q.lines[g_input_q.head]);
        g_input_q.head = (g_input_q.head + 1) % INPUT_QUEUE_CAP;
        g_input_q.count--;
        ok = 1;
    }
    pthread_mutex_unlock(&g_input_q.mtx);
    return ok;
}

static void *stdin_reader_thread(void *arg)
{
    (void)arg;
    char line[1024];
    while (!g_input_stop) {
        if (!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) {
                g_input_eof = 1;
                break;
            }
            if (ferror(stdin)) {
                clearerr(stdin);
                usleep(10000);
                continue;
            }
            continue;
        }

        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (line[0] == '\0') continue;

        if (g_runtime_running && g_runtime_path_busy) {
            if (strcmp(line, "--stop") == 0) {
                g_runtime_abort = 1;
                printf("\n[input] 已收到 STOP，正在中斷目前路徑段...\n");
                fflush(stdout);
            }

            char pbuf[256];
            if (parse_llh_file_inline(line, pbuf)) {
                if (enqueue_path_file_name(pbuf)) {
                    printf("\n[input] 已加入待執行路徑: %s\n", pbuf);
                    print_pending_path_queue();
                } else {
                    printf("\n[input] 路徑佇列已滿(%d)，已忽略: %s\n", MAX_PATH_QUEUE, pbuf);
                }
                fflush(stdout);
                continue;
            }
            if (strcmp(line, "--file-delete") == 0) {
                char removed[256] = {0};
                if (delete_last_queued_path(removed, sizeof(removed))) {
                    printf("\n[input] 已刪除佇列最後路徑: %s\n", removed);
                } else {
                    printf("\n[input] 已忽略 --file-delete：目前僅剩正在執行中的最後一段\n");
                }
                print_pending_path_queue();
                fflush(stdout);
                continue;
            }
        }

        if (strcmp(line, "--file-delete") == 0 &&
            g_runtime_running && g_runtime_path_busy && g_runtime_path_queue_count <= 0) {
            printf("\n[input] 已忽略 --file-delete：目前僅剩正在執行中的最後一段\n");
            fflush(stdout);
            continue;
        }

        if (!input_queue_push(line)) {
            printf("\n[input] 命令佇列已滿，已忽略: %s\n", line);
            fflush(stdout);
            continue;
        }

        if (g_runtime_running) {
            if (g_runtime_path_busy) {
                printf("\n[input] 已收到命令，將於目前路徑段結束後處理: %s\n", line);
            } else {
                printf("\n[input] 已收到命令: %s\n", line);
            }
            fflush(stdout);
        }
    }
    return NULL;
}

static void usage(const char *p)
{
    printf("用法: %s\n", p);
    puts("  --screen N          指定 GUI 顯示在由左到右排序的第 N 個螢幕 (例如 --screen 3)");
    puts("互動命令:");
    puts("  左上地圖左鍵點選      設定起始固定座標 (lat,lon,h=0m)");
    puts("  --llh lat,lon,h      (相容保留) 手動設定起始固定座標");
    puts("  其餘參數            請在 GUI 控制面板設定");
    puts("  --llh-file file      執行軌跡檔，完成後停在最後一點持續發射");
    puts("  --file-delete        刪除佇列中最後一段待執行軌跡");
    puts("  --start              (可選) 與 GUI START 同功能");
    puts("  --stop               (可選) 與 GUI STOP 同功能，立即中斷並清空路徑/座標");
    puts("  --help               顯示說明");
    puts("  --exit               離開程式\n");
    puts("流程:");
    puts("  1) 在左上 OpenStreetMap 左鍵點一下選起點（可重複改）");
    puts("  2) 在 GUI 左下控制面板設定其他參數");
    puts("  3) 按 GUI 的 START 開始發射 (或輸入 --start)");
    puts("  4) START 後：左鍵點地圖會依道路規劃路徑，右鍵點地圖可建立直線路徑，皆先以虛線預覽");
    puts("     右鍵雙擊代表確認，虛線轉實線並開始依路徑移動（確認前可反覆改目標點與路徑類型）");
    puts("  5) 每段確認後可用該段終點接續下一段，最多同時保留 5 段；走完會自動清除該段軌跡");
    puts("  6) START 後右上角可用返回鍵撤回最後一段（若該段已在執行中則不可撤回）");
    puts("  7) 每段皆採 0->勻速->0 的速度剖面，按 STOP 會中斷並清空路徑/座標（需重新點選起點）\n");
}

static void gui_report_alert(int level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    map_gui_push_alert(level, buf);
}

static const char *kBncRinexDir = "./BRDM";

static int is_leap_year(int year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0) ? 1 : 0;
}

static int parse_brdc_hour_utc(const char *path, time_t *t_out)
{
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    if (strncmp(fname, "BRDC00WRD_S_", 12) != 0) return -1;

    int year = 0;
    int doy = 0;
    int hh = 0;
    if (sscanf(fname + 12, "%4d%3d%2d00_01H_MN", &year, &doy, &hh) != 3) return -1;
    if (year < 1980 || year > 2100 || hh < 0 || hh > 23) return -1;

    int max_doy = is_leap_year(year) ? 366 : 365;
    if (doy < 1 || doy > max_doy) return -1;

    struct tm tmv = {0};
    tmv.tm_year = year - 1900;
    tmv.tm_mon = 0;
    tmv.tm_mday = 1;
    tmv.tm_hour = hh;

    time_t t = timegm(&tmv);
    if (t == (time_t)-1) return -1;
    t += (time_t)(doy - 1) * 86400;

    *t_out = t;
    return 0;
}

static void find_latest_rinex(char *out, size_t size)
{
    out[0] = '\0';

    DIR *dp = opendir(kBncRinexDir);
    if (!dp) return;

    time_t best_t = (time_t)0;
    bool has_best = false;
    char best_path[PATH_MAX] = {0};

    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;
        if (strncmp(name, "BRDC00WRD_S_", 12) != 0) continue;

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", kBncRinexDir, name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        time_t t_file;
        if (parse_brdc_hour_utc(full, &t_file) != 0) continue;

        if (!has_best || t_file > best_t || (t_file == best_t && strcmp(full, best_path) > 0)) {
            best_t = t_file;
            has_best = true;
            snprintf(best_path, sizeof(best_path), "%s", full);
        }
    }

    closedir(dp);

    if (has_best) {
        snprintf(out, size, "%s", best_path);
    }
}

static int rinex_is_within_2h(const char *path)
{
    time_t t_file;
    if (parse_brdc_hour_utc(path, &t_file) != 0) return 0;
    time_t t_now = time(NULL);
    return fabs(difftime(t_now, t_file)) <= 7200.0;
}

static long long current_utc_hour_key(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);

    return (long long)(tmv.tm_year + 1900) * 1000000LL +
           (long long)(tmv.tm_mon + 1) * 10000LL +
           (long long)tmv.tm_mday * 100LL +
           (long long)tmv.tm_hour;
}

static int should_try_hourly_rinex_refresh(long long *last_hour_key)
{
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);

    if (tmv.tm_min < 5) return 0;

    long long hour_key = current_utc_hour_key();
    if (*last_hour_key == hour_key) return 0;

    *last_hour_key = hour_key;
    return 1;
}

static void refresh_latest_rinex_if_needed(sim_config_t *cfg, double reload_bdt)
{
    char latest_rinex[256] = {0};
    find_latest_rinex(latest_rinex, sizeof(latest_rinex));
    if (latest_rinex[0] == '\0') {
        fprintf(stderr, "[rinex] 每小時檢查：找不到可用星曆檔 (%s/BRDC00WRD_S_YYYYDDDHH00_01H_MN*)\n", kBncRinexDir);
        return;
    }

    if (!rinex_is_within_2h(latest_rinex)) {
        fprintf(stderr, "[rinex] 每小時檢查：最新星曆超過 2 小時，保留目前星曆\n");
        return;
    }

    char old_rinex[256] = {0};
    snprintf(old_rinex, sizeof(old_rinex), "%s", cfg->rinex_file);

    snprintf(cfg->rinex_file, sizeof(cfg->rinex_file), "%s", latest_rinex);
    if (!reload_simulator_nav(cfg, reload_bdt)) {
        snprintf(cfg->rinex_file, sizeof(cfg->rinex_file), "%s", old_rinex);
        fprintf(stderr, "[rinex] 每小時檢查：重載失敗，保留原星曆 %s\n", old_rinex);
        return;
    }

    map_gui_set_rinex_name(cfg->rinex_file);
    if (strcmp(old_rinex, cfg->rinex_file) == 0) {
        printf("[rinex] 每小時 05 分後已重新載入星曆: %s\n", cfg->rinex_file);
    } else {
        printf("[rinex] 每小時 05 分後自動切換星曆: %s -> %s\n", old_rinex, cfg->rinex_file);
    }
}

static int split_args(char *line, char **argv_out, int max_args)
{
    int n = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok && n < max_args) {
        argv_out[n++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    return n;
}

static double mode_tx_center_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_CENTER_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_CENTER_HZ;
    return RF_BDS_ONLY_CENTER_HZ;
}

static double mode_min_fs_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_MIN_FS_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_MIN_FS_HZ;
    return RF_BDS_ONLY_MIN_FS_HZ;
}

static double clamp_fs_to_mode_grid(double fs_hz, uint8_t signal_mode)
{
    const double step = RF_FS_STEP_HZ;
    const double min_fs = mode_min_fs_hz(signal_mode);
    if (fs_hz < min_fs) fs_hz = min_fs;

    double k = floor(fs_hz / step + 0.5);
    if (k < 1.0) k = 1.0;
    fs_hz = k * step;

    if (fs_hz < min_fs) fs_hz = min_fs;
    return fs_hz;
}

static void apply_mode_if_offsets(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) {
        g_bds_if_offset_hz = RF_BDS_ONLY_IF_OFFSET_HZ;
        g_gps_if_offset_hz = RF_GPS_ONLY_IF_OFFSET_HZ;
    } else if (signal_mode == SIG_MODE_MIXED) {
        g_bds_if_offset_hz = RF_MIXED_BDS_IF_OFFSET_HZ;
        g_gps_if_offset_hz = RF_MIXED_GPS_IF_OFFSET_HZ;
    } else {
        g_bds_if_offset_hz = RF_BDS_ONLY_IF_OFFSET_HZ;
        g_gps_if_offset_hz = RF_GPS_ONLY_IF_OFFSET_HZ;
    }
}

static int check_internet_connectivity(void)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    if (inet_pton(AF_INET, "1.1.1.1", &addr.sin_addr) != 1) {
        return 0;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        close(fd);
        return 1;
    }
    if (errno != EINPROGRESS) {
        close(fd);
        return 0;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    rc = select(fd + 1, NULL, &wfds, NULL, &tv);
    if (rc > 0 && FD_ISSET(fd, &wfds)) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
        close(fd);
        return err == 0;
    }

    close(fd);
    return 0;
}

/* Keep legacy bool flag and new enum in sync before Fs/frequency decisions. */
static uint8_t normalize_signal_mode(uint8_t signal_mode, bool signal_gps)
{
    if (signal_mode > SIG_MODE_MIXED) {
        return signal_gps ? SIG_MODE_GPS : SIG_MODE_BDS;
    }
    if (signal_mode == SIG_MODE_BDS && signal_gps) {
        return SIG_MODE_GPS;
    }
    return signal_mode;
}

static void set_cfg_defaults(sim_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->gain = 1.0;
    cfg->target_cn0 = 45.0;
    cfg->step_ms = 1;
    cfg->duration = 1;
    cfg->seed = 1;
    cfg->byte_output = true;
    cfg->meo_only = false;
    cfg->single_prn = 0;
    cfg->prn37_only = false;
    cfg->signal_gps = false;
    cfg->signal_mode = SIG_MODE_BDS;
    cfg->interference_mode = false;
    cfg->interference_selection = -1;
    cfg->fs = mode_min_fs_hz(cfg->signal_mode);
    cfg->iono_on = true;
    cfg->tx_gain = 50.0;
    cfg->usrp_external_clk = true;
    cfg->print_ch_info = true;
    cfg->max_ch = 16;
}

static int parse_llh_csv(const char *s, double llh_deg[3])
{
    if (sscanf(s, "%lf,%lf,%lf", &llh_deg[0], &llh_deg[1], &llh_deg[2]) != 3) return -1;
    return 0;
}

static int validate_llh_deg(const double llh_deg[3])
{
    if (llh_deg[0] < -90.0 || llh_deg[0] > 90.0) return -1;
    if (llh_deg[1] < -180.0 || llh_deg[1] > 180.0) return -1;
    if (llh_deg[2] < -1000.0 || llh_deg[2] > 20000.0) return -1;
    return 0;
}

static int parse_line_command(char *line, sim_config_t *cfg, bool *llh_set, line_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));

    char *args[128];
    int argc = split_args(line, args, 128);
    for (int i = 0; i < argc; i++) {
        const char *t = args[i];

        if (strcmp(t, "--start") == 0) {
            cmd->start = true;
            continue;
        }
        if (strcmp(t, "--stop") == 0) {
            cmd->stop = true;
            continue;
        }
        if (strcmp(t, "--file-delete") == 0) {
            cmd->file_delete = true;
            continue;
        }
        if (strcmp(t, "--help") == 0) {
            cmd->help = true;
            continue;
        }
        if (strcmp(t, "--exit") == 0 || strcmp(t, "exit") == 0 || strcmp(t, "quit") == 0) {
            cmd->quit = true;
            continue;
        }
        if ((strcmp(t, "--llh") == 0 || strcmp(t, "-l") == 0) && i + 1 < argc) {
            if (parse_llh_csv(args[++i], cfg->llh) != 0) {
                fprintf(stderr, "--llh 格式應為 lat,lon,h\n");
                return -1;
            }
            if (validate_llh_deg(cfg->llh) != 0) {
                fprintf(stderr, "--llh 超出合理範圍\n");
                return -1;
            }
            *llh_set = true;
            continue;
        }
        if (strncmp(t, "--llh=", 6) == 0) {
            if (parse_llh_csv(t + 6, cfg->llh) != 0) {
                fprintf(stderr, "--llh 格式應為 lat,lon,h\n");
                return -1;
            }
            if (validate_llh_deg(cfg->llh) != 0) {
                fprintf(stderr, "--llh 超出合理範圍\n");
                return -1;
            }
            *llh_set = true;
            continue;
        }
        if (strcmp(t, "--llh-file") == 0 && i + 1 < argc) {
            strncpy(cmd->path_file, args[++i], sizeof(cmd->path_file) - 1);
            cmd->path_file[sizeof(cmd->path_file) - 1] = '\0';
            cmd->has_path_file = true;
            continue;
        }
        if (strncmp(t, "--llh-file=", 11) == 0) {
            strncpy(cmd->path_file, t + 11, sizeof(cmd->path_file) - 1);
            cmd->path_file[sizeof(cmd->path_file) - 1] = '\0';
            cmd->has_path_file = true;
            continue;
        }
        if (strcmp(t, "--prn") == 0 || strncmp(t, "--prn=", 6) == 0 ||
            strcmp(t, "--prn37") == 0 ||
            strcmp(t, "--tx-gain") == 0 || strncmp(t, "--tx-gain=", 10) == 0 ||
            strcmp(t, "--gain") == 0 || strncmp(t, "--gain=", 7) == 0 ||
            strcmp(t, "--fs") == 0 || strncmp(t, "--fs=", 5) == 0 ||
            strcmp(t, "--seed") == 0 || strncmp(t, "--seed=", 7) == 0 ||
            strcmp(t, "--byte") == 0 || strcmp(t, "-byte") == 0 ||
            strcmp(t, "--meo-only") == 0 || strcmp(t, "--no-iono") == 0 ||
            strcmp(t, "-cn0") == 0) {
            fprintf(stderr, "[info] %s 已改由 GUI 控制面板設定\n", t);
            if ((strcmp(t, "--prn") == 0 || strcmp(t, "--tx-gain") == 0 ||
                 strcmp(t, "--gain") == 0 || strcmp(t, "--fs") == 0 ||
                 strcmp(t, "--seed") == 0 || strcmp(t, "-cn0") == 0) && i + 1 < argc) {
                ++i;
            }
            continue;
        }

        fprintf(stderr, "未知參數或不支援: %s\n", t);
        return -1;
    }

    return 0;
}

static int parse_runtime_command(char *line, line_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));

    char *args[128];
    int argc = split_args(line, args, 128);
    for (int i = 0; i < argc; i++) {
        const char *t = args[i];

        if (strcmp(t, "--stop") == 0) {
            cmd->stop = true;
            continue;
        }
        if (strcmp(t, "--file-delete") == 0) {
            cmd->file_delete = true;
            continue;
        }
        if (strcmp(t, "--help") == 0) {
            cmd->help = true;
            continue;
        }
        if (strcmp(t, "--exit") == 0 || strcmp(t, "exit") == 0 || strcmp(t, "quit") == 0) {
            cmd->quit = true;
            continue;
        }
        if (strcmp(t, "--llh-file") == 0 && i + 1 < argc) {
            strncpy(cmd->path_file, args[++i], sizeof(cmd->path_file) - 1);
            cmd->path_file[sizeof(cmd->path_file) - 1] = '\0';
            cmd->has_path_file = true;
            continue;
        }
        if (strncmp(t, "--llh-file=", 11) == 0) {
            strncpy(cmd->path_file, t + 11, sizeof(cmd->path_file) - 1);
            cmd->path_file[sizeof(cmd->path_file) - 1] = '\0';
            cmd->has_path_file = true;
            continue;
        }

        fprintf(stderr,
            "[warn] 執行中僅接受 --llh-file / --file-delete / --stop（以及 --help/--exit），已忽略: %s\n",
            t);
    }

    return 0;
}

static void bdt_to_utc_string(double bdt, char out[32])
{
    const time_t bdt0 = 1136073600;
    extern int utc_bdt_diff;

    int week = (int)(bdt / 604800.0);
    double sow = bdt - week * 604800.0;
    if (sow < 0.0) sow = 0.0;

    time_t t = bdt0 + (time_t)week * 604800 + (time_t)floor(sow) - utc_bdt_diff;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, 32, "%Y/%m/%d,%H:%M:%S", &tmv);
}

static int load_last_llh_from_path(const char *file, double out_llh_deg[3], uint32_t *duration_sec)
{
    path_t path = {0};
    if (load_path_llh(file, &path) != 0 || path.n <= 0) {
        free_path(&path);
        return -1;
    }

    coord_t last = {0};
    interpolate_path(&path, (double)(path.n - 1) / PATH_UPDATE_HZ, &last);
    ecef_to_lla(last.xyz, &last);

    out_llh_deg[0] = last.llh[0] * 180.0 / M_PI;
    out_llh_deg[1] = last.llh[1] * 180.0 / M_PI;
    out_llh_deg[2] = last.llh[2];
    *duration_sec = (uint32_t)(((double)(path.n - 1) / PATH_UPDATE_HZ) + 0.5);

    free_path(&path);
    return 0;
}

static void print_start_summary(const sim_config_t *cfg, double start_bdt)
{
    int week = (int)(start_bdt / 604800.0);
    double sow = start_bdt - week * 604800.0;

    coord_t usr = {0};
    double llh_rad[3] = {
        cfg->llh[0] * M_PI / 180.0,
        cfg->llh[1] * M_PI / 180.0,
        cfg->llh[2]
    };
    lla_to_ecef(llh_rad, &usr);
    usr.week = week;
    usr.sow = sow;

    coord_t ref_llh = usr;
    static_user_at(usr.week, usr.sow, &ref_llh, &usr, NULL);

    const char *sys_label = "BDS";
    if (cfg->signal_mode == SIG_MODE_GPS) sys_label = "GPS";
    else if (cfg->signal_mode == SIG_MODE_MIXED) sys_label = "BDS+GPS";

    if (cfg->interference_mode) {
        char utc_buf[32] = {0};
        bdt_to_utc_string(start_bdt, utc_buf);
        printf("[cfg] UTC %s  BDT W%d %.3f\n", utc_buf, usr.week, usr.sow);
        printf("[cfg] LLH %.6f %.6f %.1f\n", cfg->llh[0], cfg->llh[1], cfg->llh[2]);
        printf("[cfg] XYZ %.3f %.3f %.3f (m)\n", usr.xyz[0], usr.xyz[1], usr.xyz[2]);
        printf("[cfg] MODE INTERFERENCE  SYS %s\n", sys_label);
        printf("[cfg] BPSK chip-rate: BDS %.3f MHz, GPS %.3f MHz\n", CHIPRATE / 1e6, GPS_CA_CHIPRATE / 1e6);
        printf("[cfg] Fs %.2fMHz  Gain %.2f\n\n", cfg->fs / 1e6, cfg->gain);
        return;
    }

    channel_t ch[MAX_CH];
    int n_ch = 0;
    select_channels(ch, &n_ch, &usr, cfg->single_prn, cfg->meo_only, cfg->prn37_only, cfg->signal_mode, cfg->max_ch);

    int prn_sorted[MAX_CH];
    int sys_sorted[MAX_CH];
    for (int i = 0; i < n_ch; i++) {
        prn_sorted[i] = ch[i].prn;
        sys_sorted[i] = ch[i].is_gps ? 1 : 0;
    }
    for (int i = 0; i < n_ch - 1; i++) {
        for (int j = i + 1; j < n_ch; j++) {
            int key_i = sys_sorted[i] * 100 + prn_sorted[i];
            int key_j = sys_sorted[j] * 100 + prn_sorted[j];
            if (key_j < key_i) {
                int t = prn_sorted[i];
                prn_sorted[i] = prn_sorted[j];
                prn_sorted[j] = t;
                t = sys_sorted[i];
                sys_sorted[i] = sys_sorted[j];
                sys_sorted[j] = t;
            }
        }
    }

    char utc_buf[32] = {0};
    bdt_to_utc_string(start_bdt, utc_buf);
    printf("[cfg] UTC %s  BDT W%d %.3f\n", utc_buf, usr.week, usr.sow);
    printf("[cfg] LLH %.6f %.6f %.1f\n", cfg->llh[0], cfg->llh[1], cfg->llh[2]);
    printf("[cfg] XYZ %.3f %.3f %.3f (m)\n", usr.xyz[0], usr.xyz[1], usr.xyz[2]);

    printf("[cfg] SYS %s\n", sys_label);
    printf("[cfg] PRN:");
    for (int i = 0; i < n_ch; i++) printf(" %c%02d", sys_sorted[i] ? 'G' : 'C', prn_sorted[i]);
    if (cfg->signal_mode == SIG_MODE_BDS) {
        printf("\n[cfg] IGSO:");
        for (int i = 0; i < n_ch; i++) if (is_igso_prn(prn_sorted[i])) printf(" %02d", prn_sorted[i]);
        printf("\n[cfg] MEO:");
        for (int i = 0; i < n_ch; i++) if (is_meo_prn(prn_sorted[i])) printf(" %02d", prn_sorted[i]);
    }

    printf("\n[cfg] Fs %.2fMHz  Gain %.2f\n\n", cfg->fs / 1e6, cfg->gain);
}

static int get_current_bdt(double *bdt_out)
{
    struct timespec now_ts;
    clock_gettime(CLOCK_REALTIME, &now_ts);
    struct tm tmv;
    gmtime_r(&now_ts.tv_sec, &tmv);

    char now_utc[64];
    snprintf(now_utc, sizeof(now_utc), "%04d/%02d/%02d,%02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    int week = 0;
    double sow = 0.0;
    if (utc_to_bdt(now_utc, &week, &sow) != 0) return -1;
    *bdt_out = week * 604800.0 + sow;
    return 0;
}

static int build_visible_single_prn_candidates(const sim_config_t *cfg,
                                               double llh_deg[3],
                                               double bdt,
                                               int out_prn[], int out_max)
{
    if (!cfg || !out_prn || out_max <= 0) return 0;
    if (cfg->interference_mode) return 0;

    int week = (int)(bdt / 604800.0);
    double sow = bdt - week * 604800.0;
    if (sow < 0.0) sow = 0.0;

    coord_t usr = {0};
    double llh_rad[3] = {
        llh_deg[0] * M_PI / 180.0,
        llh_deg[1] * M_PI / 180.0,
        llh_deg[2]
    };
    lla_to_ecef(llh_rad, &usr);
    usr.week = week;
    usr.sow = sow;

    coord_t ref = usr;
    static_user_at(usr.week, usr.sow, &ref, &usr, NULL);

    channel_t ch[MAX_CH] = {0};
    int n_ch = 0;
    select_channels(ch, &n_ch, &usr, 0, cfg->meo_only, cfg->prn37_only, cfg->signal_mode, cfg->max_ch);

    int n = 0;
    for (int i = 0; i < n_ch && n < out_max; ++i) {
        if (ch[i].prn <= 0) continue;
        out_prn[n++] = ch[i].prn;
    }

    for (int i = 0; i < n - 1; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (out_prn[j] < out_prn[i]) {
                int t = out_prn[i];
                out_prn[i] = out_prn[j];
                out_prn[j] = t;
            }
        }
    }

    return n;
}

static void refresh_preview_prn_mask(const sim_config_t *cfg,
                                     bool llh_set,
                                     double llh_deg[3],
                                     double bdt)
{
    for (int i = 0; i < MAX_SAT; ++i) g_active_prn_mask[i] = 0;
    if (!cfg || !llh_set) return;
    if (cfg->interference_mode) return;

    if (cfg->single_prn > 0 && cfg->single_prn < MAX_SAT) {
        g_active_prn_mask[cfg->single_prn] = 1;
        return;
    }

    int week = (int)(bdt / 604800.0);
    double sow = bdt - week * 604800.0;
    if (sow < 0.0) sow = 0.0;

    coord_t usr = {0};
    double llh_rad[3] = {
        llh_deg[0] * M_PI / 180.0,
        llh_deg[1] * M_PI / 180.0,
        llh_deg[2]
    };
    lla_to_ecef(llh_rad, &usr);
    usr.week = week;
    usr.sow = sow;

    coord_t ref = usr;
    static_user_at(usr.week, usr.sow, &ref, &usr, NULL);

    channel_t ch[MAX_CH] = {0};
    int n_ch = 0;
    select_channels(ch, &n_ch, &usr, 0, cfg->meo_only, cfg->prn37_only, cfg->signal_mode, cfg->max_ch);
    for (int i = 0; i < n_ch; ++i) {
        if (ch[i].prn > 0 && ch[i].prn < MAX_SAT) {
            g_active_prn_mask[ch[i].prn] = 1;
        }
    }
}

static void stop_and_reset_runtime_state(sim_config_t *cfg,
                                         bool *llh_set,
                                         bool *running,
                                         bool *prompt_shown,
                                         bool *print_ch_next_static,
                                         bool *usrp_ready,
                                         bool *usrp_scheduled,
                                         double hold_llh[3])
{
    if (running) *running = false;
    g_runtime_running = 0;
    g_runtime_path_busy = 0;
    g_runtime_path_queue_count = 0;
    g_runtime_abort = 0;
    clear_queued_paths();
    cleanup_runtime_path_files();
    map_gui_set_run_state(0);

    if (llh_set) *llh_set = false;
    if (cfg) {
        cfg->llh[0] = 0.0;
        cfg->llh[1] = 0.0;
        cfg->llh[2] = 0.0;
    }
    if (hold_llh) {
        hold_llh[0] = 0.0;
        hold_llh[1] = 0.0;
        hold_llh[2] = 0.0;
    }
    if (print_ch_next_static) *print_ch_next_static = false;

    g_receiver_valid = 0;
    pthread_mutex_lock(&g_gui_spectrum_mtx);
    g_gui_spectrum_valid = 0;
    g_gui_time_valid = 0;
    g_gui_spectrum_bins = GUI_SPECTRUM_BINS;
    g_gui_time_samples = GUI_TIME_MON_SAMPLES;
    g_gui_spectrum_seq += 1;
    pthread_mutex_unlock(&g_gui_spectrum_mtx);
    map_gui_set_llh_ready(0);
    map_gui_clear_path_segments();
    map_gui_set_single_prn_candidates(NULL, 0);
    if (cfg) refresh_preview_prn_mask(NULL, false, cfg->llh, 0.0);

    if (usrp_ready && *usrp_ready) {
        usrp_close();
        *usrp_ready = false;
        if (usrp_scheduled) *usrp_scheduled = false;
    }

    if (prompt_shown) *prompt_shown = false;
}

int main(int argc, char *argv[])
{
    cuda_runtime_apply_safe_env();
    const char *disable_jit = getenv("CUDA_DISABLE_JIT");
    const char *disable_ptx_jit = getenv("CUDA_DISABLE_PTX_JIT");
    if ((disable_jit && strcmp(disable_jit, "1") == 0) ||
        (disable_ptx_jit && strcmp(disable_ptx_jit, "1") == 0)) {
        fprintf(stderr, "[cuda] JIT disabled (set BDS_CUDA_DISABLE_JIT=0 to re-enable PTX fallback).\n");
    } else {
        fprintf(stderr, "[cuda] JIT enabled (PTX fallback available when needed).\n");
    }
    const int cuda_runtime_enabled = cuda_runtime_is_enabled_by_env();
    int cuda_runtime_smoke_ok = 1;
    if (cuda_runtime_enabled && cuda_runtime_should_run_smoke()) {
        cuda_runtime_smoke_ok = cuda_runtime_probe_safely(cuda_runtime_smoke_test);
        if (!cuda_runtime_smoke_ok) {
            fprintf(stderr,
                    "[error] CUDA runtime probe failed (segfault or init error in child process).\n"
                    "[error] 建議使用與 Driver CUDA 版本相符的 Toolkit（你目前 Driver 顯示 CUDA 13.1，建議安裝/切換到 CUDA 13.1 nvcc）。\n");
        }
    }

    int gui_screen_index = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            gui_screen_index = atoi(argv[++i]);
            continue;
        }
        if (strncmp(argv[i], "--screen=", 9) == 0) {
            gui_screen_index = atoi(argv[i] + 9);
            continue;
        }
    }

    sim_config_t cfg;
    set_cfg_defaults(&cfg);
    map_gui_set_control_defaults(&cfg);
    map_gui_set_screen_index(gui_screen_index);

    bool llh_set = false;
    bool running = false;
    bool gui_started = false;
    bool usrp_ready = false;
    bool usrp_scheduled = false;
    bool simulator_ready = false;
    bool print_ch_next_static = false;
    double usrp_rate_in_use = 0.0;
    double usrp_gain_in_use = 0.0;
    double usrp_freq_in_use = 0.0;
    bool usrp_external_clk_in_use = true;
    bool usrp_byte_in_use = false;
    double next_bdt = 0.0;
    long long last_hourly_rinex_refresh_key = -1;
    double hold_llh[3] = {0};
    clear_queued_paths();

    find_latest_rinex(cfg.rinex_file, sizeof(cfg.rinex_file));
    map_gui_set_rinex_name(cfg.rinex_file);
        int internet_ok = check_internet_connectivity();
        int catalog_ok = (cfg.rinex_file[0] != '\0' && rinex_is_within_2h(cfg.rinex_file));
        int spoof_allowed_now = (internet_ok && catalog_ok);
        map_gui_set_mode_policy(spoof_allowed_now ? 1 : 0);
        if (!catalog_ok && internet_ok) {
        fprintf(stderr,
            "[warn] 有網路但沒有可用星曆，僅可使用 JAM 模式\n");
        gui_report_alert(1,
                 "Network is available but no valid RINEX file is loaded. Only JAM mode is available.");
        } else if (!catalog_ok && !internet_ok) {
        fprintf(stderr,
            "[warn] 無網路且沒有可用星曆，僅可使用 JAM 模式\n");
        gui_report_alert(1,
                 "No internet and no valid RINEX file. Only JAM mode is available.");
        }

        if (cfg.rinex_file[0] == '\0') {
        fprintf(stderr,
                "[warn] 找不到可用星曆檔 (%s/BRDC00WRD_S_YYYYDDDHH00_01H_MN*)，僅可使用 JAM 模式\n",
                kBncRinexDir);
        gui_report_alert(1,
                         "No valid RINEX file found. Only JAM mode is available.");
    } else if (!rinex_is_within_2h(cfg.rinex_file)) {
        fprintf(stderr,
                "[warn] 最新星曆檔超過 2 小時，僅可使用 JAM 模式或更新 %s 後再啟動一般模式\n",
                kBncRinexDir);
        gui_report_alert(1,
                         "Latest RINEX is older than 2 hours. Update BRDM before using general mode.");
    }

    usage("./bds-sim");
    puts("[bds-sim] 已待命，先在左上地圖點選起點，其他參數在 GUI 左下設定，再按 START");

    struct timespec gui_now_ts;
    clock_gettime(CLOCK_REALTIME, &gui_now_ts);
    struct tm gui_tmv;
    gmtime_r(&gui_now_ts.tv_sec, &gui_tmv);
    char gui_now_utc[64];
    snprintf(gui_now_utc, sizeof(gui_now_utc), "%04d/%02d/%02d,%02d:%02d:%02d",
             gui_tmv.tm_year + 1900, gui_tmv.tm_mon + 1, gui_tmv.tm_mday,
             gui_tmv.tm_hour, gui_tmv.tm_min, gui_tmv.tm_sec);
    int gui_week = 0;
    double gui_sow = 0.0;
    if (utc_to_bdt(gui_now_utc, &gui_week, &gui_sow) == 0) {
        double gui_bdt = gui_week * 604800.0 + gui_sow;

        if (cfg.rinex_file[0] != '\0' && rinex_is_within_2h(cfg.rinex_file)) {
            if (init_simulator(&cfg, gui_bdt)) {
                simulator_ready = true;
            } else {
                fprintf(stderr, "[warn] GUI 待命初始化失敗，將在 --start 時重試\n");
                gui_report_alert(1, "GUI standby initialization failed. It will retry on START.");
            }
        }

        start_map_gui(gui_bdt);
        gui_started = true;
        map_gui_set_run_state(0);
        map_gui_set_llh_ready(0);
    }

    char line[1024];
    bool prompt_shown = false;
    time_t preview_refresh_sec = (time_t)-1;
    pthread_t input_tid;
    if (pthread_create(&input_tid, NULL, stdin_reader_thread, NULL) != 0) {
        fprintf(stderr, "[error] 無法建立輸入執行緒\n");
        gui_report_alert(2, "Failed to create input thread.");
        return 1;
    }

    while (1) {
        if (!running && !prompt_shown) {
            printf("bds-sim> ");
            fflush(stdout);
            prompt_shown = true;
        }

        if (!input_queue_pop(line, sizeof(line))) {
            line[0] = '\0';
            if (g_input_eof) break;
        } else {
            prompt_shown = false;
        }

        bool gui_start = map_gui_consume_start_request() != 0;
        bool gui_stop = map_gui_consume_stop_request() != 0;
        bool gui_exit = map_gui_consume_exit_request() != 0;
        double picked_lat = 0.0, picked_lon = 0.0, picked_h = 0.0;
        bool gui_picked_llh = map_gui_consume_selected_llh(&picked_lat, &picked_lon, &picked_h) != 0;
        line_cmd_t cmd = {0};

        if (gui_picked_llh && !running) {
            cfg.llh[0] = picked_lat;
            cfg.llh[1] = picked_lon;
            cfg.llh[2] = picked_h;
            llh_set = true;
            printf("[cfg] 地圖起點已更新 LLH: %.6f,%.6f,%.2f\n", cfg.llh[0], cfg.llh[1], cfg.llh[2]);
        }

        if (line[0] != '\0') {
            if (running) {
                if (parse_runtime_command(line, &cmd) != 0) {
                    cmd = (line_cmd_t){0};
                }
            } else {
                if (parse_line_command(line, &cfg, &llh_set, &cmd) != 0) {
                    continue;
                }
            }

            if (cmd.help) {
                usage("./bds-sim");
                if (!running) continue;
            }
            if (cmd.quit) {
                break;
            }
        }

        if (gui_start) cmd.start = true;
        if (gui_stop) cmd.stop = true;
        if (gui_exit) cmd.quit = true;

        if (cmd.quit) {
            break;
        }

        if (!running) {
            g_runtime_running = 0;
            g_runtime_path_busy = 0;
            g_runtime_path_queue_count = 0;

            sim_config_t preview_cfg = cfg;
            map_gui_get_control_config(&preview_cfg, &g_target_cn0);
            preview_cfg.signal_mode = normalize_signal_mode(preview_cfg.signal_mode, preview_cfg.signal_gps);
            preview_cfg.signal_gps = (preview_cfg.signal_mode == SIG_MODE_GPS);
            preview_cfg.fs = clamp_fs_to_mode_grid(preview_cfg.fs, preview_cfg.signal_mode);

            find_latest_rinex(cfg.rinex_file, sizeof(cfg.rinex_file));
            map_gui_set_rinex_name(cfg.rinex_file);

            const bool spoof_selected = (preview_cfg.interference_selection == 0);
            const bool jam_selected = (preview_cfg.interference_selection == 1);
            const bool llh_required = spoof_selected;

            if (spoof_selected && !llh_set && !g_spoof_autocenter_done) {
                double auto_lat = 0.0, auto_lon = 0.0, auto_h = 0.0;
                if (map_gui_get_default_spoof_llh(&auto_lat, &auto_lon, &auto_h)) {
                    cfg.llh[0] = auto_lat;
                    cfg.llh[1] = auto_lon;
                    cfg.llh[2] = auto_h;
                    llh_set = true;
                    map_gui_set_selected_llh(auto_lat, auto_lon, auto_h);
                    fprintf(stderr,
                            "[cfg] SPOOF 預設定位至最近禁航區中心: %.6f,%.6f,%.2f\n",
                            auto_lat, auto_lon, auto_h);
                    g_spoof_autocenter_done = 1;
                }
            }

            map_gui_set_llh_ready((llh_set || !llh_required || jam_selected) ? 1 : 0);
            if (llh_set) {
                g_receiver_lat_deg = cfg.llh[0];
                g_receiver_lon_deg = cfg.llh[1];
                g_receiver_valid = 1;
            } else {
                g_receiver_valid = 0;
            }

            if (!llh_set && llh_required) {
                map_gui_set_single_prn_candidates(NULL, 0);
                refresh_preview_prn_mask(NULL, false, cfg.llh, 0.0);
                if (cmd.start) {
                    fprintf(stderr, "[error] SPOOF 模式需要定位，且無法自動取得預設 NFZ 中心\n");
                    gui_report_alert(2, "SPOOF mode requires a location, and no NFZ center could be determined.");
                    map_gui_set_run_state(0);
                }
                usleep(20000);
                continue;
            }

            time_t now_sec = time(NULL);
            if (now_sec != preview_refresh_sec || line[0] != '\0') {
                double now_bdt = 0.0;
                if (llh_set && get_current_bdt(&now_bdt) == 0) {
                    int cand[64] = {0};
                    int cand_n = build_visible_single_prn_candidates(&preview_cfg, cfg.llh, now_bdt, cand, 64);
                    map_gui_set_single_prn_candidates(cand, cand_n);
                    map_gui_get_control_config(&preview_cfg, &g_target_cn0);
                    refresh_preview_prn_mask(&preview_cfg, llh_set, cfg.llh, now_bdt);
                } else {
                    map_gui_set_single_prn_candidates(NULL, 0);
                    refresh_preview_prn_mask(NULL, false, cfg.llh, 0.0);
                }
                preview_refresh_sec = now_sec;
            }

            if (!cmd.start) {
                if (line[0] != '\0') {
                    printf("[cfg] 目前起始 LLH: %.6f,%.6f,%.2f\n", cfg.llh[0], cfg.llh[1], cfg.llh[2]);
                    puts("[cfg] 其餘參數請在 GUI 左下控制面板設定，按 START 開始。");
                }
                usleep(20000);
                continue;
            }

            if (preview_cfg.interference_selection < 0) {
                fprintf(stderr, "[error] 請先選擇 INTERFERE 的模式 (SPOOF 或 JAM)\n");
                gui_report_alert(2, "Please choose INTERFERE mode first: SPOOF or JAM.");
                map_gui_set_run_state(0);
                continue;
            }

            map_gui_set_tx_active(0);

            map_gui_get_control_config(&cfg, &g_target_cn0);
            cfg.signal_mode = normalize_signal_mode(cfg.signal_mode, cfg.signal_gps);
            cfg.signal_gps = (cfg.signal_mode == SIG_MODE_GPS);
            cfg.fs = clamp_fs_to_mode_grid(cfg.fs, cfg.signal_mode);
            apply_mode_if_offsets(cfg.signal_mode);

            if (!cfg.interference_mode) {
                find_latest_rinex(cfg.rinex_file, sizeof(cfg.rinex_file));
                map_gui_set_rinex_name(cfg.rinex_file);
                if (cfg.rinex_file[0] == '\0') {
                    fprintf(stderr,
                            "[error] 一般模式需要星曆：找不到可用星曆檔 (%s/BRDC00WRD_S_YYYYDDDHH00_01H_MN*)\n",
                            kBncRinexDir);
                    gui_report_alert(2,
                                     "General mode requires a valid RINEX file, but none was found.");
                    map_gui_set_run_state(0);
                    continue;
                }
                if (!rinex_is_within_2h(cfg.rinex_file)) {
                    fprintf(stderr,
                            "[error] 一般模式需要 2 小時內星曆，請先更新 %s\n",
                            kBncRinexDir);
                    gui_report_alert(2,
                                     "General mode requires RINEX data within 2 hours. Please update BRDM.");
                    map_gui_set_run_state(0);
                    continue;
                }
            }

            pthread_mutex_lock(&g_gui_spectrum_mtx);
            g_gui_spectrum_valid = 0;
            g_gui_time_valid = 0;
            g_gui_spectrum_bins = GUI_SPECTRUM_BINS;
            g_gui_time_samples = GUI_TIME_MON_SAMPLES;
            g_gui_spectrum_seq += 1;
            pthread_mutex_unlock(&g_gui_spectrum_mtx);

            double tx_freq = mode_tx_center_hz(cfg.signal_mode);
            double tx_rate = cfg.fs;

            if (usrp_ready && (fabs(usrp_rate_in_use - tx_rate) > 1.0 ||
                               fabs(usrp_gain_in_use - cfg.tx_gain) > 1e-6 ||
                               fabs(usrp_freq_in_use - tx_freq) > 1.0 ||
                               usrp_external_clk_in_use != cfg.usrp_external_clk ||
                               usrp_byte_in_use != cfg.byte_output)) {
                usrp_close();
                usrp_ready = false;
                usrp_scheduled = false;
            }
            if (!usrp_ready) {
                if (usrp_init(tx_freq, tx_rate, cfg.tx_gain,
                              cfg.usrp_external_clk ? 1 : 0,
                              cfg.byte_output ? USRP_SAMPLE_FMT_BYTE : USRP_SAMPLE_FMT_SHORT) != 0) {
                    fprintf(stderr, "[warn] USRP 初始化失敗或未連線，將僅產生檔案輸出\n");
                    gui_report_alert(1, "USRP initialization failed or device is disconnected. File output mode will be used.");
                } else {
                    usrp_ready = true;
                    usrp_freq_in_use = tx_freq;
                    usrp_rate_in_use = tx_rate;
                    usrp_gain_in_use = cfg.tx_gain;
                    usrp_external_clk_in_use = cfg.usrp_external_clk;
                    usrp_byte_in_use = cfg.byte_output;
                }
            }

            struct timespec now_ts;
            clock_gettime(CLOCK_REALTIME, &now_ts);
            struct tm tmv;
            gmtime_r(&now_ts.tv_sec, &tmv);
            char now_utc[64];
            snprintf(now_utc, sizeof(now_utc), "%04d/%02d/%02d,%02d:%02d:%02d",
                     tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                     tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

            int start_week = 0;
            double start_sow = 0.0;
            if (utc_to_bdt(now_utc, &start_week, &start_sow) != 0) {
                fprintf(stderr, "[error] 目前時間轉換失敗\n");
                gui_report_alert(2, "Current time conversion failed.");
                map_gui_set_run_state(0);
                continue;
            }
            next_bdt = start_week * 604800.0 + start_sow + 1.0;

            char start_utc[32] = {0};
            bdt_to_utc_string(next_bdt, start_utc);

            if (!cfg.interference_mode && !simulator_ready) {
                if (!init_simulator(&cfg, next_bdt)) {
                    fprintf(stderr, "[error] 初始化失敗\n");
                    gui_report_alert(2, "Simulator initialization failed.");
                    map_gui_set_run_state(0);
                    continue;
                }
                simulator_ready = true;
            }

            if (!cuda_runtime_enabled) {
                fprintf(stderr,
                        "[error] CUDA 執行已被停用。若要啟用請移除 BDS_DISABLE_CUDA 或設為 0\n");
                gui_report_alert(2,
                                 "CUDA runtime is disabled. Remove BDS_DISABLE_CUDA (or set it to 0) to enable.");
                map_gui_set_run_state(0);
                continue;
            }

            if (!cuda_runtime_smoke_ok) {
                gui_report_alert(2,
                                 "CUDA runtime probe failed in this environment (driver/toolkit mismatch likely). "
                                 "Use matching CUDA toolkit version (e.g., driver 13.1 with nvcc 13.1). "
                                 "Set BDS_SKIP_CUDA_SMOKE=1 to force run.");
                map_gui_set_run_state(0);
                continue;
            }

            // Switch to running UI only after all start prechecks pass.
            map_gui_set_run_state(1);

            if (!gui_started) {
                start_map_gui(next_bdt);
                gui_started = true;
            }
            update_map_gui_start(next_bdt);
            map_gui_set_rinex_name(cfg.rinex_file);

            if (!cfg.interference_mode) {
                printf("[cfg] 使用最新星曆: %s\n", cfg.rinex_file);
            } else {
                puts("[cfg] JAM 模式不使用星曆");
            }
            printf("[cfg] 起始時間使用當前主機 UTC: %s\n", start_utc);

            if (!usrp_scheduled && usrp_ready) {
                usrp_schedule_start_in(1.0);
                usrp_scheduled = true;
            }

            print_start_summary(&cfg, next_bdt);
            reset_signal_engine_state();
            g_runtime_abort = 0;

            hold_llh[0] = cfg.llh[0];
            hold_llh[1] = cfg.llh[1];
            hold_llh[2] = cfg.llh[2];
            print_ch_next_static = true;

              running = true;
                 g_runtime_running = 1;
                 clear_queued_paths();
                        map_gui_set_tx_active(1);
            printf("[run] 開始連續發射固定點 %.6f, %.6f, %.2f\n",
                   hold_llh[0], hold_llh[1], hold_llh[2]);
                 map_gui_clear_path_segments();
                 puts("[run] 執行中可輸入: --llh-file <file> / --file-delete / --stop");
            continue;
        }

        if (cmd.stop) {
            stop_and_reset_runtime_state(&cfg, &llh_set, &running, &prompt_shown,
                                         &print_ch_next_static,
                                         &usrp_ready, &usrp_scheduled,
                                         hold_llh);
            puts("[run] 已停止並清空所有路徑與座標，請重新在地圖點選起點");
            continue;
        }

        map_gui_get_control_config(&cfg, &g_target_cn0);
        cfg.signal_mode = normalize_signal_mode(cfg.signal_mode, cfg.signal_gps);
        cfg.signal_gps = (cfg.signal_mode == SIG_MODE_GPS);
        cfg.fs = clamp_fs_to_mode_grid(cfg.fs, cfg.signal_mode);
        apply_mode_if_offsets(cfg.signal_mode);

        if (cmd.file_delete) {
            char removed[256] = {0};
            if (delete_last_queued_path(removed, sizeof(removed))) {
                printf("[run] 已移除佇列最後一段: %s\n", removed);
                map_gui_notify_path_segment_undo();
                print_pending_path_queue();
            } else {
                puts("[run] 佇列為空，僅有目前執行段落（或固定點），不刪除");
                print_pending_path_queue();
            }
        }

        if (cmd.has_path_file) {
            if (enqueue_path_file_name(cmd.path_file)) {
                printf("[run] 已加入路徑佇列: %s\n", cmd.path_file);
                print_pending_path_queue();
            } else {
                fprintf(stderr, "[warn] 路徑佇列已滿 (%d)，已忽略: %s\n", MAX_PATH_QUEUE, cmd.path_file);
                gui_report_alert(1, "Path queue is full (max 5 segments). New path was ignored.");
            }
        }

        queued_path_t q = {0};
        if (pop_first_queued_path(&q)) {
            double last_llh[3] = {0};
            uint32_t traj_dur = 0;
            if (load_last_llh_from_path(q.path_file, last_llh, &traj_dur) != 0) {
                fprintf(stderr, "[error] 軌跡檔讀取失敗，已跳過: %s\n", q.path_file);
                gui_report_alert(2, "Path file read failed. The segment was skipped.");
                continue;
            }

            sim_config_t run_cfg = cfg;
            run_cfg.path_type = 2;
            run_cfg.duration = traj_dur;
            run_cfg.print_ch_info = true;
            run_cfg.llh[0] = hold_llh[0];
            run_cfg.llh[1] = hold_llh[1];
            run_cfg.llh[2] = hold_llh[2];
            strncpy(run_cfg.path_file, q.path_file, sizeof(run_cfg.path_file) - 1);
            run_cfg.path_file[sizeof(run_cfg.path_file) - 1] = '\0';
            bdt_to_utc_string(next_bdt, run_cfg.time_start);
            g_enable_iono = run_cfg.iono_on ? 1 : 0;

            printf("[run] 執行軌跡: %s (%u 秒)\n", q.path_file, traj_dur);
            print_pending_path_queue();
                 g_runtime_path_busy = 1;
              map_gui_notify_path_segment_started();
            generate_signal(&run_cfg);
                 g_runtime_path_busy = 0;

              if (g_runtime_abort) {
                 stop_and_reset_runtime_state(&cfg, &llh_set, &running, &prompt_shown,
                                        &print_ch_next_static,
                                        &usrp_ready, &usrp_scheduled,
                                        hold_llh);
                 puts("[run] 已中斷目前路徑並清空所有路徑與座標，請重新在地圖點選起點");
                 continue;
              }

            next_bdt += (double)traj_dur;
            hold_llh[0] = last_llh[0];
            hold_llh[1] = last_llh[1];
            hold_llh[2] = last_llh[2];
            print_ch_next_static = true;
                 map_gui_notify_path_segment_finished();
            printf("[run] 軌跡完成，固定在最後點 %.6f, %.6f, %.2f\n",
                   hold_llh[0], hold_llh[1], hold_llh[2]);
            continue;
        }

        sim_config_t run_cfg = cfg;
        run_cfg.path_type = 0;
        run_cfg.duration = 1;
        run_cfg.print_ch_info = print_ch_next_static;

        if (!cfg.interference_mode && should_try_hourly_rinex_refresh(&last_hourly_rinex_refresh_key)) {
            refresh_latest_rinex_if_needed(&cfg, next_bdt);
        }

        run_cfg.llh[0] = hold_llh[0];
        run_cfg.llh[1] = hold_llh[1];
        run_cfg.llh[2] = hold_llh[2];
        snprintf(run_cfg.rinex_file, sizeof(run_cfg.rinex_file), "%s", cfg.rinex_file);
        bdt_to_utc_string(next_bdt, run_cfg.time_start);
        g_enable_iono = run_cfg.iono_on ? 1 : 0;

        generate_signal(&run_cfg);
        if (g_runtime_abort) {
            stop_and_reset_runtime_state(&cfg, &llh_set, &running, &prompt_shown,
                                         &print_ch_next_static,
                                         &usrp_ready, &usrp_scheduled,
                                         hold_llh);
            puts("[run] 已中斷並清空所有路徑與座標，請重新在地圖點選起點");
            continue;
        }
        print_ch_next_static = false;
        next_bdt += 1.0;
    }

    g_input_stop = 1;
    pthread_cancel(input_tid);
    pthread_join(input_tid, NULL);

    if (gui_started) stop_map_gui();
    if (usrp_ready) usrp_close();
    if (simulator_ready) cleanup_simulator();
    cleanup_runtime_path_files();
    return 0;
}
