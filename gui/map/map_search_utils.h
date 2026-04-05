#ifndef MAP_SEARCH_UTILS_H
#define MAP_SEARCH_UTILS_H

#include <QJsonArray>
#include <QString>
#include <QUrl>

#include <vector>

struct MapSearchResult {
  double lat = 0.0;
  double lon = 0.0;
  QString line;
  QString display_name;
};

bool map_search_parse_coordinate_query(const QString &query, double *lat,
                                       double *lon);
QUrl map_search_nominatim_url(const QString &query, int limit = 8);
std::vector<MapSearchResult>
map_search_parse_nominatim_results(const QJsonArray &arr,
                                   int max_results = 8);

#endif