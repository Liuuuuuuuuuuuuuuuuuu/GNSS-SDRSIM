/* rinex.c – 解析 BDS RINEX3.04 NAV，僅提取 MEO/IGSO D1 需要的欄位
 * (c) 2025 your-name */

#include "timeconv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include "rinex.h"
#include "bdssim.h"
#include "globals.h"

static double bdt_seconds_to_gpst_seconds(double bdt_sec)
{
    const double bdt0 = 1136073600.0; /* 2006-01-01 */
    const double gps0 = 315964800.0;  /* 1980-01-06 */
    extern int utc_bdt_diff;
    extern int utc_gpst_diff;

    /* UTC = BDT - (UTC->BDT offset), GPST = UTC + (UTC->GPST offset) */
    double utc_sec = bdt0 + bdt_sec - (double)utc_bdt_diff;
    double gpst_sec = utc_sec - gps0 + (double)utc_gpst_diff;
    return gpst_sec;
}

static int is_leap_year_local(int year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0) ? 1 : 0;
}

static int parse_brdc_hour_utc_local(const char *path, time_t *t_out)
{
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;

    if (strncmp(fname, "BRDC00WRD_S_", 12) != 0) return -1;

    int year = 0;
    int doy = 0;
    int hh = 0;
    if (sscanf(fname + 12, "%4d%3d%2d00_01H_MN", &year, &doy, &hh) != 3) return -1;
    if (year < 1980 || year > 2100 || hh < 0 || hh > 23) return -1;

    int max_doy = is_leap_year_local(year) ? 366 : 365;
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

static int find_second_latest_rinex_sibling(const char *primary_path, char *out, size_t out_sz)
{
    if (!primary_path || !out || out_sz == 0) return -1;
    out[0] = '\0';

    char dir[PATH_MAX] = {0};
    const char *slash = strrchr(primary_path, '/');
    if (!slash) {
        snprintf(dir, sizeof(dir), ".");
    } else {
        size_t n = (size_t)(slash - primary_path);
        if (n >= sizeof(dir)) n = sizeof(dir) - 1;
        memcpy(dir, primary_path, n);
        dir[n] = '\0';
    }

    DIR *dp = opendir(dir);
    if (!dp) return -1;

    time_t best_t = (time_t)0;
    time_t second_t = (time_t)0;
    int has_best = 0;
    int has_second = 0;
    char best_path[PATH_MAX] = {0};
    char second_path[PATH_MAX] = {0};

    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        if (!name || name[0] == '.') continue;

        char full[PATH_MAX] = {0};
        snprintf(full, sizeof(full), "%s/%s", dir, name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        time_t t_file = (time_t)0;
        if (parse_brdc_hour_utc_local(full, &t_file) != 0) continue;

        if (!has_best || t_file > best_t || (t_file == best_t && strcmp(full, best_path) > 0)) {
            if (has_best) {
                second_t = best_t;
                has_second = 1;
                snprintf(second_path, sizeof(second_path), "%s", best_path);
            }
            best_t = t_file;
            has_best = 1;
            snprintf(best_path, sizeof(best_path), "%s", full);
        } else if (!has_second || t_file > second_t || (t_file == second_t && strcmp(full, second_path) > 0)) {
            second_t = t_file;
            has_second = 1;
            snprintf(second_path, sizeof(second_path), "%s", full);
        }
    }

    closedir(dp);

    if (!has_second) return -1;
    if (strcmp(best_path, primary_path) != 0) {
        /* primary_path is expected to be latest; if not, use best as fallback candidate */
        snprintf(out, out_sz, "%s", best_path);
    } else {
        snprintf(out, out_sz, "%s", second_path);
    }
    return 0;
}

static int print_loaded_bds_prns(void)
{
    int count = 0;
    printf("[rinex] BDS PRN 清單:");
    for (int prn = 1; prn <= BDS_PRN_MAX; ++prn) {
        if (eph[prn].prn == 0) continue;
        printf(" C%02d", prn);
        ++count;
    }
    if (count == 0) {
        printf(" (none)");
    }
    printf("\n");
    return count;
}

static int print_loaded_gps_prns(void)
{
    int count = 0;
    printf("[rinex] GPS PRN 清單:");
    for (int prn = 1; prn <= GPS_PRN_MAX; ++prn) {
        if (gps_eph[prn].prn == 0) continue;
        printf(" G%02d", prn);
        ++count;
    }
    if (count == 0) {
        printf(" (none)");
    }
    printf("\n");
    return count;
}

/* --------------------------- 小工具 ------------------------------*/
static double fld(const char *ln, int idx, int ind)
{
    char buf[20];
    int pos = ind + idx * 19;
    if ((int)strlen(ln) <= pos) return 0.0;
    memcpy(buf, ln + pos, 19);
    buf[19] = '\0';
    for (int i = 0; i < 19; i++)
        if (buf[i] == 'D' || buf[i] == 'd') buf[i] = 'E';
    char *end;
    double v = strtod(buf, &end);
    return (end == buf) ? 0.0 : v;
}
static int ifld(const char *ln, int idx, int ind)
{
    return (int)fld(ln, idx, ind);
}

/* --------------------------- very-tiny, zone-less timegm (UTC→UNIX)  –  只考慮閏年規則，可跨平臺 ------------------------------*/

/* --------------------------- 主要解析 ------------------------------*/
int read_rinex_nav_bds(const char *fname, double start_bdt)
{
    FILE *fp = fopen(fname, "r");
    if (!fp) { perror(fname);  return -1; }

    char l[120];

    /* parse header */
    while (fgets(l, sizeof(l), fp))
    {
        if(strncmp(l,"BDSA",4)==0){
            sscanf(l+4, "%lf %lf %lf %lf", &iono_alpha[0], &iono_alpha[1],
                   &iono_alpha[2], &iono_alpha[3]);
            continue;
        }
        if(strncmp(l,"BDSB",4)==0){
            sscanf(l+4, "%lf %lf %lf %lf", &iono_beta[0], &iono_beta[1],
                   &iono_beta[2], &iono_beta[3]);
            continue;
        }
        if (strstr(l, "END OF HEADER")) break;
    }

    const int IND1 = 23;   /* 第一行 offset */
    const int INDN = 4;    /* 後 7 行 offset */

    nav_time_min = 1e20;
    nav_time_max = -1e20;
    double best_diff[BDS_EPH_SLOTS];
    for (int i = 0; i < BDS_EPH_SLOTS; i++) best_diff[i] = 1e20;

    while (fgets(l, sizeof(l), fp))
    {
        if (l[0] != 'C' || !isdigit(l[1]))          /* 只處理北斗 */
            continue;

        int prn = atoi(l + 1);
        if (prn < 1 || prn > BDS_PRN_MAX) {         /* out of range：跳過 7 行 */
            char skip[120];
            for (int i = 0; i < 7; ++i)
                if (!fgets(skip, sizeof skip, fp)) break;
            continue;
        }

        bds_ephemeris_t tmp = {0};
        tmp.prn = prn;

        /* 1st row：時間戳記與 af0/1/2 */
        int yy, mm, dd, hh, mi;
        double ss;
        int w_dummy; double sow;
        int toc_sec = -1;
        if (sscanf(l + 4, "%d %d %d %d %d %lf", &yy, &mm, &dd, &hh, &mi, &ss) == 6) {
            char utc_str[32];
            snprintf(utc_str, sizeof utc_str,
                     "%04d/%02d/%02d,%02d:%02d:%02d",
                     yy, mm, dd, hh, mi, (int)ss);
            if (utc_to_bdt(utc_str, &w_dummy, &sow) == 0) {
                toc_sec = ((int)sow - utc_bdt_diff) % 604800;
                if (toc_sec < 0) toc_sec += 604800;
            }
        }
        (void)w_dummy;
        tmp.af0 = fld(l, 0, IND1);
        tmp.af1 = fld(l, 1, IND1);
        tmp.af2 = fld(l, 2, IND1);

        /* 讀後 7 行 */

        char r[7][120];
        for (int i = 0; i < 7; ++i)
            if (!fgets(r[i], sizeof r[i], fp)) return -1;

        tmp.aode    = ifld(r[0], 0, INDN);
        tmp.crs     = fld(r[0], 1, INDN);
        tmp.deltan  = fld(r[0], 2, INDN);
        tmp.M0      = fld(r[0], 3, INDN);

        tmp.cuc     = fld(r[1], 0, INDN);
        tmp.e       = fld(r[1], 1, INDN);
        tmp.cus     = fld(r[1], 2, INDN);
        tmp.sqrtA   = fld(r[1], 3, INDN);

        tmp.toe     = fld(r[2], 0, INDN);
        tmp.toc     = (toc_sec >= 0) ? toc_sec : (int)tmp.toe; /* fallback: toe */
        tmp.cic     = fld(r[2], 1, INDN);
        tmp.omega0  = fld(r[2], 2, INDN);
        tmp.cis     = fld(r[2], 3, INDN);

        tmp.i0      = fld(r[3], 0, INDN);
        tmp.crc     = fld(r[3], 1, INDN);
        tmp.w       = fld(r[3], 2, INDN);
        tmp.omegadot= fld(r[3], 3, INDN);

        tmp.idot    = fld(r[4], 0, INDN);
        int week_r = ifld(r[4], 2, INDN);  /* BDS week number */
        tmp.week = week_r;

        /* line 7: SV accuracy/health and TGD1/2 */
        tmp.ura     = ifld(r[5], 0, INDN) & 0xF;
        tmp.health  = ifld(r[5], 1, INDN) & 0x1;
        if (tmp.health == 1)                /* SatH1=1 ⇒ skip satellite */
            continue;
        tmp.tgd1    = fld(r[5], 2, INDN);
        tmp.tgd2    = fld(r[5], 3, INDN);

        /* line 8: AODC */
        tmp.aodc    = ifld(r[6], 1, INDN);

        double t_bdt = tmp.week * 604800.0 + tmp.toe;
        if (t_bdt < nav_time_min) nav_time_min = t_bdt;
        if (t_bdt > nav_time_max) nav_time_max = t_bdt;

        double diff = fabs(t_bdt - start_bdt);
        if(diff < best_diff[prn]){
            best_diff[prn] = diff;
            eph[prn] = tmp;
        }
    }

    fclose(fp);
    int loaded_bds = print_loaded_bds_prns();
    if (loaded_bds <= 0) {
        fprintf(stderr, "[rinex][warn] 未載入任何 BDS 星曆（檔案可能不含 Cxx 記錄）\n");
        return -1;
    }
    printf("[rinex] 北斗星曆已載入 (%d)\n", loaded_bds);
    return 0;
}

int read_rinex_nav_gps(const char *fname, double start_gpst)
{
    FILE *fp = fopen(fname, "r");
    if (!fp) { perror(fname); return -1; }

    char l[120];

    while (fgets(l, sizeof(l), fp))
        if (strstr(l, "END OF HEADER")) break;

    const int IND1 = 23;
    const int INDN = 4;

    double best_diff[GPS_EPH_SLOTS];
    for (int i = 0; i < GPS_EPH_SLOTS; ++i) best_diff[i] = 1e20;

    while (fgets(l, sizeof(l), fp)) {
        if (l[0] != 'G' || !isdigit((unsigned char)l[1]))
            continue;

        int prn = atoi(l + 1);
        if (prn < 1 || prn > GPS_PRN_MAX) {
            char skip[120];
            for (int i = 0; i < 7; ++i)
                if (!fgets(skip, sizeof skip, fp)) break;
            continue;
        }

        gps_ephemeris_t tmp = {0};
        tmp.prn = prn;

        int yy, mm, dd, hh, mi;
        double ss;
        int w_dummy;
        double sow;
        int toc_sec = -1;
        if (sscanf(l + 4, "%d %d %d %d %d %lf", &yy, &mm, &dd, &hh, &mi, &ss) == 6) {
            char utc_str[32];
            snprintf(utc_str, sizeof utc_str,
                     "%04d/%02d/%02d,%02d:%02d:%02d",
                     yy, mm, dd, hh, mi, (int)ss);
            if (utc_to_gpst(utc_str, &w_dummy, &sow) == 0)
                toc_sec = (int)sow;
        }

        tmp.af0 = fld(l, 0, IND1);
        tmp.af1 = fld(l, 1, IND1);
        tmp.af2 = fld(l, 2, IND1);

        char r[7][120];
        for (int i = 0; i < 7; ++i)
            if (!fgets(r[i], sizeof r[i], fp)) return -1;

        tmp.iode    = ifld(r[0], 0, INDN);
        tmp.crs     = fld(r[0], 1, INDN);
        tmp.deltan  = fld(r[0], 2, INDN);
        tmp.M0      = fld(r[0], 3, INDN);

        tmp.cuc     = fld(r[1], 0, INDN);
        tmp.e       = fld(r[1], 1, INDN);
        tmp.cus     = fld(r[1], 2, INDN);
        tmp.sqrtA   = fld(r[1], 3, INDN);

        tmp.toe     = fld(r[2], 0, INDN);
        tmp.toc     = (toc_sec >= 0) ? toc_sec : (int)tmp.toe;
        tmp.cic     = fld(r[2], 1, INDN);
        tmp.omega0  = fld(r[2], 2, INDN);
        tmp.cis     = fld(r[2], 3, INDN);

        tmp.i0      = fld(r[3], 0, INDN);
        tmp.crc     = fld(r[3], 1, INDN);
        tmp.w       = fld(r[3], 2, INDN);
        tmp.omegadot= fld(r[3], 3, INDN);

        tmp.idot    = fld(r[4], 0, INDN);
        tmp.codeL2  = ifld(r[4], 1, INDN); /* line 6 第二個欄位 */
        tmp.week    = ifld(r[4], 2, INDN);

        tmp.ura     = ifld(r[5], 0, INDN) & 0xF;
        tmp.health  = ifld(r[5], 1, INDN) & 0x3F;
        tmp.tgd     = fld(r[5], 2, INDN);
        tmp.iodc    = ifld(r[5], 3, INDN);

        tmp.Fit     = ifld(r[6], 1, INDN);

        double t_gps = tmp.week * 604800.0 + tmp.toe;
        double diff = fabs(t_gps - start_gpst);
        if (diff < best_diff[prn]) {
            best_diff[prn] = diff;
            gps_eph[prn] = tmp;
        }
    }

    fclose(fp);
    int loaded_gps = print_loaded_gps_prns();
    if (loaded_gps <= 0) {
        fprintf(stderr, "[rinex][warn] 未載入任何 GPS 星曆（檔案可能不含 Gxx 記錄）\n");
        return -1;
    }
    printf("[rinex] GPS 星曆已載入 (%d)\n", loaded_gps);
    return 0;
}

int read_rinex_nav(const char *fname, double start_bdt)
{
    double start_gpst = bdt_seconds_to_gpst_seconds(start_bdt);
    int ret_bds = read_rinex_nav_bds(fname, start_bdt);
    int ret_gps = read_rinex_nav_gps(fname, start_gpst);

    if (ret_bds != 0 || ret_gps != 0) {
        char fallback[PATH_MAX] = {0};
        if (find_second_latest_rinex_sibling(fname, fallback, sizeof(fallback)) == 0) {
            if (ret_bds != 0) {
                int fb_bds = read_rinex_nav_bds(fallback, start_bdt);
                if (fb_bds == 0) {
                    ret_bds = 0;
                    printf("[rinex] BDS 由次新星曆補齊: %s\n", fallback);
                }
            }
            if (ret_gps != 0) {
                int fb_gps = read_rinex_nav_gps(fallback, start_gpst);
                if (fb_gps == 0) {
                    ret_gps = 0;
                    printf("[rinex] GPS 由次新星曆補齊: %s\n", fallback);
                }
            }
        } else {
            fprintf(stderr, "[rinex][warn] 找不到次新星曆可補齊缺少系統\n");
        }
    }

    if (ret_bds != 0 && ret_gps != 0) return -1;
    return 0;
}
/* ---------------------------  End  ------------------------------*/
