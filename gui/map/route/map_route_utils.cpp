#include "gui/map/route/map_route_utils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

QString map_route_cache_key(double lat0, double lon0, double lat1, double lon1) {
  return QString("%1,%2|%3,%4")
  .arg(lat0, 0, 'f', 3)
  .arg(wrap_lon_deg(lon0), 0, 'f', 3)
  .arg(lat1, 0, 'f', 3)
  .arg(wrap_lon_deg(lon1), 0, 'f', 3);
}

QString map_route_osrm_url(double lat0, double lon0, double lat1, double lon1) {
  return QString("https://router.project-osrm.org/route/v1/driving/"
         "%1,%2;%3,%4?overview=simplified&geometries=geojson&steps=false&alternatives=false&annotations=false")
      .arg(lon0, 0, 'f', 7)
      .arg(lat0, 0, 'f', 7)
      .arg(lon1, 0, 'f', 7)
      .arg(lat1, 0, 'f', 7);
}

bool map_route_parse_osrm_geojson(const QByteArray &body,
                                  std::vector<LonLat> *polyline) {
  if (!polyline)
    return false;

  QJsonParseError perr;
  QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
  if (perr.error != QJsonParseError::NoError || !doc.isObject())
    return false;

  QJsonObject root = doc.object();
  QJsonArray routes = root.value("routes").toArray();
  if (routes.isEmpty() || !routes.at(0).isObject())
    return false;

  QJsonObject route0 = routes.at(0).toObject();
  QJsonObject geom = route0.value("geometry").toObject();
  QJsonArray coords = geom.value("coordinates").toArray();
  if (coords.size() < 2)
    return false;

  constexpr int kMaxPreviewPoints = 320;
  int stride = 1;
  if (coords.size() > kMaxPreviewPoints) {
    stride = std::max(1, coords.size() / kMaxPreviewPoints);
  }

  std::vector<LonLat> out;
  out.reserve((size_t)((coords.size() + stride - 1) / stride + 2));
  for (int i = 0; i < coords.size(); i += stride) {
    QJsonArray pt = coords.at(i).toArray();
    if (pt.size() < 2)
      continue;
    out.push_back({pt.at(0).toDouble(), pt.at(1).toDouble()});
  }

  if ((coords.size() - 1) % stride != 0) {
    const QJsonArray pt_last = coords.at(coords.size() - 1).toArray();
    if (pt_last.size() >= 2) {
      out.push_back({pt_last.at(0).toDouble(), pt_last.at(1).toDouble()});
    }
  }

  if (out.size() < 2)
    return false;

  *polyline = std::move(out);
  return true;
}

void map_route_trim_cache(
    std::unordered_map<QString, std::vector<LonLat>> &cache,
    std::vector<QString> &order, int max_entries) {
  if (max_entries < 1)
    max_entries = 1;
  if ((int)order.size() <= max_entries)
    return;

  int drop_n = (int)order.size() - max_entries;
  for (int i = 0; i < drop_n; ++i) {
    const QString &old_key = order[(size_t)i];
    cache.erase(old_key);
  }
  order.erase(order.begin(), order.begin() + drop_n);
}
