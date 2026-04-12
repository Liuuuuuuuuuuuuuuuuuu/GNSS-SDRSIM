#ifndef RINEX_H
#define RINEX_H

#include <stdint.h>

#define BDS_PRN_MAX 63
#define GPS_PRN_MAX 32

#define BDS_EPH_SLOTS (BDS_PRN_MAX + 1)
#define GPS_EPH_SLOTS (GPS_PRN_MAX + 1)

typedef struct ephemeris {
    int    prn, week;
    double toe;
    double sqrtA, e, i0, omega0, w, M0;
    double deltan, idot, omegadot;
    double cuc, cus, cic, cis, crc, crs;
    double af0, af1, af2;
    int     aode;
    double  tgd1, tgd2;
    int     aodc;
    int     toc;
    uint8_t ura;
    uint8_t health;
} bds_ephemeris_t;

typedef struct gps_ephemeris {
    int    prn, week;
    double toe;
    double sqrtA, e, i0, omega0, w, M0;
    double deltan, idot, omegadot;
    double cuc, cus, cic, cis, crc, crs;
    double af0, af1, af2;
    int     iode;
    double  tgd;
    int     iodc;
    int     Fit;
    int     toc;
    uint8_t ura;
    uint8_t health;
    int     codeL2;
} gps_ephemeris_t;

int read_rinex_nav(const char *file_bds, const char *file_gps, double start_bdt);
int read_rinex_nav_bds(const char *file, double start_bdt);
int read_rinex_nav_gps(const char *file, double start_gpst);

#endif /* RINEX_H */


