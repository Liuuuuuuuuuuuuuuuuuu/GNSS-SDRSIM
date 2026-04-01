#include "gui/osm_projection.h"

#include <cmath>

static inline double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

double osm_world_size_for_zoom(int z)
{
    return 256.0 * (double)(1u << z);
}

double osm_lon_to_world_x(double lon_deg, int zoom)
{
    double lon = lon_deg;
    while (lon < -180.0) lon += 360.0;
    while (lon >= 180.0) lon -= 360.0;
    double world = osm_world_size_for_zoom(zoom);
    return (lon + 180.0) / 360.0 * world;
}

double osm_lat_to_world_y(double lat_deg, int zoom)
{
    double lat = clamp_double(lat_deg, -85.05112878, 85.05112878);
    double lat_rad = lat * M_PI / 180.0;
    double world = osm_world_size_for_zoom(zoom);
    double merc_n = std::log(std::tan(M_PI / 4.0 + lat_rad / 2.0));
    return (1.0 - merc_n / M_PI) * 0.5 * world;
}

double osm_world_x_to_lon(double world_x, int zoom)
{
    double world = osm_world_size_for_zoom(zoom);
    double x = world_x;
    while (x < 0.0) x += world;
    while (x >= world) x -= world;
    return x / world * 360.0 - 180.0;
}

double osm_world_y_to_lat(double world_y, int zoom)
{
    double world = osm_world_size_for_zoom(zoom);
    double y = world_y;
    if (y < 0.0) y = 0.0;
    if (y > world - 1.0) y = world - 1.0;
    double n = M_PI * (1.0 - 2.0 * y / world);
    return std::atan(std::sinh(n)) * 180.0 / M_PI;
}

void osm_normalize_center(int zoom, double *center_x, double *center_y)
{
    if (!center_x || !center_y) return;

    double world = osm_world_size_for_zoom(zoom);
    while (*center_x < 0.0) *center_x += world;
    while (*center_x >= world) *center_x -= world;
    if (*center_y < 0.0) *center_y = 0.0;
    if (*center_y > world - 1.0) *center_y = world - 1.0;
}
