#ifndef RINEX_CATALOG_H
#define RINEX_CATALOG_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *rinex_catalog_resolve_dir(void);
double rinex_catalog_max_age_seconds(void);
int rinex_catalog_parse_brdc_hour_utc(const char *path, time_t *t_out);
int rinex_catalog_is_within_age(const char *path, double max_age_seconds);
void rinex_catalog_find_latest_paths(char *out_bds, size_t size_bds,
                                     char *out_gps, size_t size_gps,
                                     int min_sats_per_system);

#ifdef __cplusplus
}
#endif

#endif
