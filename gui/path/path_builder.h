#ifndef PATH_BUILDER_H
#define PATH_BUILDER_H

#include <vector>

#include "gui/geo/geo_io.h"

enum {
    PATH_MODE_PLAN = 0,
    PATH_MODE_LINE = 1
};

bool build_segment_path_file(double lat0_deg, double lon0_deg,
                             double lat1_deg, double lon1_deg,
                             int mode,
                             double vmax_mps,
                             double accel_mps2,
                             const std::vector<LonLat> *plan_polyline,
                             char out_path[256],
                             std::vector<LonLat> *out_polyline);

#endif
