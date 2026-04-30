#include "rinex_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *kBncRinexDirs[] = {
    "./BRDM",
    "./data/ephemeris/BRDM"
};

static int is_leap_year(int year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    return (year % 4 == 0) ? 1 : 0;
}

const char *rinex_catalog_resolve_dir(void)
{
    struct stat st;
    for (size_t i = 0; i < sizeof(kBncRinexDirs) / sizeof(kBncRinexDirs[0]); ++i) {
        const char *dir = kBncRinexDirs[i];
        if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
            return dir;
        }
    }
    return kBncRinexDirs[0];
}

double rinex_catalog_max_age_seconds(void)
{
    const double kDefaultHours = 168.0; /* 7 days */
    const char *env = getenv("BDS_RINEX_MAX_AGE_HOURS");
    if (!env || env[0] == '\0') {
        return kDefaultHours * 3600.0;
    }

    char *end = NULL;
    double hours = strtod(env, &end);
    if (end == env || !isfinite(hours) || hours <= 0.0) {
        return kDefaultHours * 3600.0;
    }

    if (hours > 24.0 * 30.0) {
        hours = 24.0 * 30.0;
    }
    return hours * 3600.0;
}

int rinex_catalog_parse_brdc_hour_utc(const char *path, time_t *t_out)
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

int rinex_catalog_is_within_age(const char *path, double max_age_seconds)
{
    time_t t_file;
    if (rinex_catalog_parse_brdc_hour_utc(path, &t_file) != 0) return 0;
    time_t t_now = time(NULL);
    return fabs(difftime(t_now, t_file)) <= max_age_seconds;
}

static void rinex_scan_system_counts(const char *path, int *bds_count, int *gps_count)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        *bds_count = 0;
        *gps_count = 0;
        return;
    }

    int bds_seen[100] = {0};
    int gps_seen[100] = {0};
    int bds_total = 0;
    int gps_total = 0;

    char line[160];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "END OF HEADER")) break;
    }

    while (fgets(line, sizeof(line), fp)) {
        const char sys = line[0];
        if (sys != 'C' && sys != 'G') continue;
        if (!isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2])) continue;

        const int prn = (line[1] - '0') * 10 + (line[2] - '0');
        if (prn < 0 || prn >= 100) continue;

        if (sys == 'C') {
            if (!bds_seen[prn]) {
                bds_seen[prn] = 1;
                ++bds_total;
            }
        } else {
            if (!gps_seen[prn]) {
                gps_seen[prn] = 1;
                ++gps_total;
            }
        }
    }

    fclose(fp);
    *bds_count = bds_total;
    *gps_count = gps_total;
}

void rinex_catalog_find_latest_paths(char *out_bds, size_t size_bds,
                                     char *out_gps, size_t size_gps,
                                     int min_sats_per_system)
{
    out_bds[0] = '\0';
    out_gps[0] = '\0';

    const char *rinex_dir = rinex_catalog_resolve_dir();
    DIR *dp = opendir(rinex_dir);
    if (!dp) return;

    time_t best_bds_t = (time_t)0;
    time_t best_gps_t = (time_t)0;
    bool has_best_bds = false;
    bool has_best_gps = false;
    char best_bds_path[PATH_MAX] = {0};
    char best_gps_path[PATH_MAX] = {0};

    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;
        if (strncmp(name, "BRDC00WRD_S_", 12) != 0) continue;

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", rinex_dir, name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        time_t t_file;
        if (rinex_catalog_parse_brdc_hour_utc(full, &t_file) != 0) continue;

        int bds_count = 0;
        int gps_count = 0;
        rinex_scan_system_counts(full, &bds_count, &gps_count);

        if (bds_count >= min_sats_per_system &&
            (!has_best_bds || t_file > best_bds_t ||
             (t_file == best_bds_t && strcmp(full, best_bds_path) > 0))) {
            best_bds_t = t_file;
            has_best_bds = true;
            snprintf(best_bds_path, sizeof(best_bds_path), "%s", full);
        }

        if (gps_count >= min_sats_per_system &&
            (!has_best_gps || t_file > best_gps_t ||
             (t_file == best_gps_t && strcmp(full, best_gps_path) > 0))) {
            best_gps_t = t_file;
            has_best_gps = true;
            snprintf(best_gps_path, sizeof(best_gps_path), "%s", full);
        }
    }

    closedir(dp);

    if (has_best_bds) snprintf(out_bds, size_bds, "%s", best_bds_path);
    if (has_best_gps) snprintf(out_gps, size_gps, "%s", best_gps_path);
}
