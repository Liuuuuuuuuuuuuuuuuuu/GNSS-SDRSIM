#ifndef MAP_ROUTE_UTILS_H
#define MAP_ROUTE_UTILS_H

#include "gui/geo/geo_io.h"

#include <QByteArray>
#include <QString>

#include <unordered_map>
#include <vector>

QString map_route_cache_key(double lat0, double lon0, double lat1, double lon1);
QString map_route_osrm_url(double lat0, double lon0, double lat1, double lon1);
bool map_route_parse_osrm_geojson(const QByteArray &body,
                                  std::vector<LonLat> *polyline);
void map_route_trim_cache(
    std::unordered_map<QString, std::vector<LonLat>> &cache,
    std::vector<QString> &order, int max_entries);

#endif