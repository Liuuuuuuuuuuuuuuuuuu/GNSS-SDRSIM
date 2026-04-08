#ifndef GEO_IO_H
#define GEO_IO_H

#include <vector>

struct LonLat {
    double lon;
    double lat;
};

double wrap_lon_deg(double lon);
double wrap_lon_delta_deg(double dlon);
double distance_m_approx(double lat0_deg, double lon0_deg, double lat1_deg, double lon1_deg);

bool load_land_shapefile(const char *shp_path, std::vector<std::vector<LonLat>> &parts_out);
int lon_to_x(double lon_deg, int width);
int lat_to_y(double lat_deg, int height);

#endif
