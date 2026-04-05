#ifndef OSM_PROJECTION_H
#define OSM_PROJECTION_H

double osm_world_size_for_zoom(int z);
double osm_lon_to_world_x(double lon_deg, int zoom);
double osm_lat_to_world_y(double lat_deg, int zoom);
double osm_world_x_to_lon(double world_x, int zoom);
double osm_world_y_to_lat(double world_y, int zoom);
void osm_normalize_center(int zoom, double *center_x, double *center_y);

#endif
