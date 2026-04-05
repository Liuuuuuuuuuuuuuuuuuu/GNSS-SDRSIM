#include "gui/map/map_search_utils.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

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

double clamp_double(double v, double lo, double hi) {
  return std::max(lo, std::min(hi, v));
}

QString normalize_for_match(QString s) {
  s = s.toLower().trimmed();
  s.replace(QRegularExpression("\\s+"), " ");
  return s;
}

double query_match_score(const QString &query_norm, const QString &primary_norm,
                         const QString &full_norm) {
  if (query_norm.isEmpty())
    return 0.0;

  double score = 0.0;
  if (primary_norm == query_norm)
    score += 4.0;
  if (full_norm == query_norm)
    score += 3.2;
  if (primary_norm.startsWith(query_norm))
    score += 2.4;
  if (full_norm.startsWith(query_norm))
    score += 2.0;
  if (primary_norm.contains(query_norm))
    score += 1.6;
  if (full_norm.contains(query_norm))
    score += 1.2;

  const QStringList tokens = query_norm.split(' ', Qt::SkipEmptyParts);
  int token_hits = 0;
  for (const QString &token : tokens) {
    if (token.size() < 2)
      continue;
    if (primary_norm.contains(token) || full_norm.contains(token))
      ++token_hits;
  }
  if (!tokens.isEmpty()) {
    score += 2.0 * (double)token_hits / (double)tokens.size();
  }

  return score;
}

double geo_bias_score(bool has_bias_center, double bias_lat, double bias_lon,
                      double lat, double lon) {
  if (!has_bias_center)
    return 0.0;

  // Equirectangular approximation is fast and good enough for ranking.
  const double deg_to_rad = 0.017453292519943295;
  const double phi1 = bias_lat * deg_to_rad;
  const double phi2 = lat * deg_to_rad;
  const double dphi = (lat - bias_lat) * deg_to_rad;
  const double dlambda = (lon - bias_lon) * deg_to_rad;
  const double x = dlambda * std::cos((phi1 + phi2) * 0.5);
  const double distance_km = 6371.0 * std::sqrt(x * x + dphi * dphi);

  if (distance_km < 2.0)
    return 2.1;
  if (distance_km < 10.0)
    return 1.5;
  if (distance_km < 30.0)
    return 1.0;
  if (distance_km < 100.0)
    return 0.5;
  if (distance_km < 300.0)
    return 0.15;
  return -0.2;
}

QString build_secondary_text(const QString &display_name) {
  const int comma = display_name.indexOf(',');
  if (comma < 0)
    return QString();
  return display_name.mid(comma + 1).trimmed();
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

QUrl map_search_nominatim_url(const QString &query, int limit,
                              bool has_bias_center, double bias_lat,
                              double bias_lon, double bias_box_deg,
                              const QString &language_tag) {
  if (limit < 1)
    limit = 1;

  QUrl url("https://nominatim.openstreetmap.org/search");
  QUrlQuery q;
  q.addQueryItem("q", query.trimmed());
  q.addQueryItem("format", "json");
  q.addQueryItem("addressdetails", "1");
  q.addQueryItem("namedetails", "1");
  q.addQueryItem("dedupe", "1");
  q.addQueryItem("limit", QString::number(limit));

  const QString lang =
      language_tag.trimmed().isEmpty() ? QLocale::system().name().replace('_', '-')
                                       : language_tag.trimmed();
  if (!lang.isEmpty()) {
    q.addQueryItem("accept-language", lang);
  }

  if (has_bias_center && bias_lat >= -90.0 && bias_lat <= 90.0 &&
      bias_lon >= -180.0 && bias_lon <= 180.0) {
    const double d = clamp_double(std::abs(bias_box_deg), 0.05, 6.0);
    const double left = clamp_double(bias_lon - d, -180.0, 180.0);
    const double right = clamp_double(bias_lon + d, -180.0, 180.0);
    const double top = clamp_double(bias_lat + d, -90.0, 90.0);
    const double bottom = clamp_double(bias_lat - d, -90.0, 90.0);
    q.addQueryItem("viewbox",
                   QString::number(left, 'f', 6) + "," +
                       QString::number(top, 'f', 6) + "," +
                       QString::number(right, 'f', 6) + "," +
                       QString::number(bottom, 'f', 6));
    // Keep global results, but prioritize nearby candidates.
    q.addQueryItem("bounded", "0");
  }

  url.setQuery(q);
  return url;
}

std::vector<MapSearchResult>
map_search_parse_nominatim_results(const QJsonArray &arr, const QString &query,
                                   int max_results, bool has_bias_center,
                                   double bias_lat, double bias_lon) {
  if (max_results < 1)
    max_results = 1;

  const QString query_norm = normalize_for_match(query);
  std::vector<MapSearchResult> out;
  out.reserve((size_t)arr.size());

  for (int i = 0; i < arr.size(); ++i) {
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
    QString primary = display.section(',', 0, 0).trimmed();
    if (primary.isEmpty())
      primary = display;
    QString secondary = build_secondary_text(display);
    const QString primary_norm = normalize_for_match(primary);
    const QString display_norm = normalize_for_match(display);

    const double importance =
        clamp_double(obj.value("importance").toDouble(0.0), 0.0, 1.0);
    const double place_rank =
        clamp_double(obj.value("place_rank").toDouble(0.0), 0.0, 30.0);

    const double relevance = query_match_score(query_norm, primary_norm, display_norm);
    const double geo_bias = geo_bias_score(has_bias_center, bias_lat, bias_lon,
                                           lat, lon);
    const double score = relevance * 1.8 + importance * 1.6 +
                         (place_rank / 30.0) * 0.5 + geo_bias;

    MapSearchResult item;
    item.lat = lat;
    item.lon = lon;
    item.display_name = display;
    item.primary_text = primary;
    item.secondary_text = secondary;
    item.ranking_score = score;
    if (secondary.isEmpty()) {
      item.line = QString("%1\n(%2, %3)")
                      .arg(primary)
                      .arg(lat, 0, 'f', 5)
                      .arg(lon, 0, 'f', 5);
    } else {
      item.line = QString("%1\n%2")
                      .arg(primary)
                      .arg(secondary);
    }
    out.push_back(std::move(item));
  }

  std::stable_sort(out.begin(), out.end(),
                   [](const MapSearchResult &a, const MapSearchResult &b) {
                     return a.ranking_score > b.ranking_score;
                   });

  if ((int)out.size() > max_results) {
    out.resize((size_t)max_results);
  }

  return out;
}
