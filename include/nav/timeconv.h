#ifndef TIMECONV_H
#define TIMECONV_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <time.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>

#if !defined(__USE_XOPEN2K) && !defined(__USE_XOPEN) && !defined(__USE_GNU)
char *strptime(const char *restrict, const char *restrict, struct tm *restrict);
#endif

static inline int utc_to_bdt(const char *utc_str, int *week, double *sow)
{
    struct tm tm = {0};
    /* Parse with fractional seconds support. Use sscanf to extract seconds as double. */
    int y, mo, d, hh, mm;
    double ss;
    if (sscanf(utc_str, "%d/%d/%d,%d:%d:%lf", &y, &mo, &d, &hh, &mm, &ss) != 6)
        return -1;

    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = (int)floor(ss);

    time_t t = timegm(&tm);
    double frac = ss - floor(ss);
    const time_t bdt0 = 1136073600;
    extern int utc_bdt_diff;
    const time_t week_sec = 604800;
    time_t diff_int = t - bdt0 + utc_bdt_diff;
    long w = diff_int / week_sec;
    long r = diff_int % week_sec;
    if (r < 0) { r += week_sec; --w; }

    *week = (int)w;
    *sow  = (double)r + frac;
    return 0;
}

static inline int utc_to_gpst(const char *utc_str, int *week, double *sow)
{
    struct tm tm = {0};
    int y, mo, d, hh, mm;
    double ss;
    if (sscanf(utc_str, "%d/%d/%d,%d:%d:%lf", &y, &mo, &d, &hh, &mm, &ss) != 6)
        return -1;

    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = (int)floor(ss);

    time_t t = timegm(&tm);
    double frac = ss - floor(ss);
    const time_t gps0 = 315964800; /* 1980-01-06 00:00:00 */
    extern int utc_gpst_diff;
    const time_t week_sec = 604800;
    time_t diff_int = t - gps0 + utc_gpst_diff;
    long w = diff_int / week_sec;
    long r = diff_int % week_sec;
    if (r < 0) { r += week_sec; --w; }

    *week = (int)w;
    *sow  = (double)r + frac;
    return 0;
}

#endif /* TIMECONV_H */

