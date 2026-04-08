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

#endif