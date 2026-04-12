#ifndef NFZ_HIT_TEST_UTILS_H
#define NFZ_HIT_TEST_UTILS_H

#include "gui/nfz/dji_nfz.h"

int nfz_zone_layer_index(const DjiNfzZone &zone);

bool nfz_pick_target_llh(const std::vector<DjiNfzZone> &zones, double click_lat,
                         double click_lon, double *target_lat,
                         double *target_lon,
                         const bool *layer_visible4 = nullptr);
bool nfz_find_nearest_zone_center(const std::vector<DjiNfzZone> &zones,
                                  double ref_lat, double ref_lon,
                                  double *target_lat, double *target_lon);

// Find NFZ center near the reverse bearing of the forward direction and inside
// [min_dist_m, max_dist_m].
// - angular gate: |delta(backward_bearing, center_bearing)| <= 3 deg
// - ranking: nearest distance first, angle is tie-breaker
// - returns false if no valid NFZ in range
// - out_nearest_reverse_dist_m reports nearest reverse-side distance regardless
//   of distance-window validity (useful for warning/diagnostic UI)
bool nfz_find_reverse_zone_center_in_range(const std::vector<DjiNfzZone> &zones,
                                           double ref_lat, double ref_lon,
                                           double forward_bearing_deg,
                                           double min_dist_m,
                                           double max_dist_m,
                                           double *target_lat,
                                           double *target_lon,
                                           double *out_dist_m,
                                           double *out_nearest_reverse_dist_m);

#endif