#include "gui/map/map_search_utils.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace {

const QRegularExpression &search_coord_regex() {
  static const QRegularExpression re(
      "^\\s*([-+]?\\d*\\.?\\d+)[\\s,]+([-+]?\\d*\\.?\\d+)\\s*$");
  return re;
}

bool parse_search_coord(const QJsonValue &v, double *out) {
  if (!out)
    return false;
  bool ok = false;
  double value = 0.0;
  if (v.isString()) {
    value = v.toString().toDouble(&ok);
  } else if (v.isDouble()) {
    value = v.toDouble();
    ok = true;
  }
  if (!ok)
    return false;
  *out = value;
  return true;
}

} // namespace

bool map_search_parse_coordinate_query(const QString &query, double *lat,
                                       double *lon) {
  if (!lat || !lon)
    return false;
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty())
    return false;

  QRegularExpressionMatch match = search_coord_regex().match(trimmed);
  if (!match.hasMatch())
    return false;

  double num1 = match.captured(1).toDouble();
  double num2 = match.captured(2).toDouble();

  if (num1 >= -90.0 && num1 <= 90.0 && num2 >= -180.0 && num2 <= 180.0) {
    *lat = num1;
    *lon = num2;
    return true;
  }
  if (num1 >= -180.0 && num1 <= 180.0 && num2 >= -90.0 && num2 <= 90.0) {
    *lon = num1;
    *lat = num2;
    return true;
  }

  return false;
}

QUrl map_search_nominatim_url(const QString &query, int limit) {
  if (limit < 1)
    limit = 1;

  QUrl url("https://nominatim.openstreetmap.org/search");
  QUrlQuery q;
  q.addQueryItem("q", query.trimmed());
  q.addQueryItem("format", "json");
  q.addQueryItem("limit", QString::number(limit));
  url.setQuery(q);
  return url;
}

std::vector<MapSearchResult>
map_search_parse_nominatim_results(const QJsonArray &arr, int max_results) {
  if (max_results < 1)
    max_results = 1;

  std::vector<MapSearchResult> out;
  out.reserve((size_t)std::min(arr.size(), max_results));

  for (int i = 0; i < arr.size(); ++i) {
    if ((int)out.size() >= max_results)
      break;

    if (!arr.at(i).isObject())
      continue;
    const QJsonObject obj = arr.at(i).toObject();

    double lat = 0.0;
    double lon = 0.0;
    if (!parse_search_coord(obj.value("lat"), &lat))
      continue;
    if (!parse_search_coord(obj.value("lon"), &lon))
      continue;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
      continue;

    QString display = obj.value("display_name").toString().trimmed();
    if (display.isEmpty()) {
      display = QString("Result %1").arg(i + 1);
    }
    QString brief = display.section(',', 0, 0).trimmed();
    if (brief.isEmpty())
      brief = display;

    MapSearchResult item;
    item.lat = lat;
    item.lon = lon;
    item.display_name = display;
    item.line = QString("%1  (%2, %3)")
                    .arg(brief)
                    .arg(lat, 0, 'f', 5)
                    .arg(lon, 0, 'f', 5);
    out.push_back(std::move(item));
  }

  return out;
}
