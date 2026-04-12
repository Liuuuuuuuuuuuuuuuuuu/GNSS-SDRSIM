#include "gui/nfz/dji_nfz_utils.h"

#include "gui/nfz/nfz_hit_test_utils.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace {

double ring_area_abs(const std::vector<DjiLonLat> &pts) {
  if (pts.size() < 3)
    return 0.0;
  double s = 0.0;
  for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
    s += (pts[j].lon * pts[i].lat) - (pts[i].lon * pts[j].lat);
  }
  return std::abs(s) * 0.5;
}

bool is_coordinate_pair(const QJsonArray &arr) {
  return arr.size() >= 2 && arr.at(0).isDouble() && arr.at(1).isDouble();
}

bool parse_ring_points(const QJsonValue &ring_val, DjiPolygonRing *out_ring) {
  if (!out_ring || !ring_val.isArray())
    return false;
  const QJsonArray ring_arr = ring_val.toArray();
  for (int i = 0; i < ring_arr.size(); ++i) {
    const QJsonValue pt_val = ring_arr.at(i);
    if (pt_val.isObject()) {
      const QJsonObject pt_obj = pt_val.toObject();
      if (pt_obj.contains("lng") && pt_obj.contains("lat")) {
        out_ring->points.push_back(
            {pt_obj.value("lng").toDouble(), pt_obj.value("lat").toDouble()});
      }
      continue;
    }
    if (pt_val.isArray()) {
      const QJsonArray pt_arr = pt_val.toArray();
      if (is_coordinate_pair(pt_arr)) {
        out_ring->points.push_back(
            {pt_arr.at(0).toDouble(), pt_arr.at(1).toDouble()});
      }
    }
  }
  return out_ring->points.size() >= 3;
}

void append_polygon_from_value(const QJsonValue &poly_val,
                               std::vector<DjiPolygonRing> *out_rings) {
  if (!out_rings || !poly_val.isArray())
    return;
  const QJsonArray poly_arr = poly_val.toArray();
  if (poly_arr.isEmpty())
    return;

  const QJsonValue first = poly_arr.at(0);
  if (first.isArray() && !is_coordinate_pair(first.toArray())) {
    for (int i = 0; i < poly_arr.size(); ++i) {
      DjiPolygonRing ring;
      ring.is_outer = (i == 0);
      if (parse_ring_points(poly_arr.at(i), &ring)) {
        out_rings->push_back(std::move(ring));
      }
    }
    return;
  }

  DjiPolygonRing outer;
  outer.is_outer = true;
  if (parse_ring_points(poly_val, &outer)) {
    out_rings->push_back(std::move(outer));
  }
}

} // namespace

int dji_nfz_draw_weight(const DjiNfzZone &zone) {
  const int layer = nfz_zone_layer_index(zone);
  switch (layer) {
  case 3:
    return 0;
  case 2:
    return 1;
  case 1:
    return 2;
  case 0:
    return 3;
  default:
    return 0;
  }
}

void dji_nfz_layer_colors(int layer, QColor *stroke, QColor *fill) {
  if (!stroke || !fill)
    return;
  switch (layer) {
  case 0:
    *stroke = QColor(220, 38, 38, 235);
    *fill = QColor(220, 38, 38, 88);
    return;
  case 1:
    *stroke = QColor(217, 119, 6, 230);
    *fill = QColor(245, 158, 11, 75);
    return;
  case 2:
    *stroke = QColor(37, 99, 235, 230);
    *fill = QColor(59, 130, 246, 60);
    return;
  default:
    *stroke = QColor(241, 245, 249, 230);
    *fill = QColor(241, 245, 249, 30);
    return;
  }
}

double dji_nfz_zone_area_score(const DjiNfzZone &z) {
  if (z.type == DjiNfzType::CIRCLE) {
    return M_PI * z.radius_m * z.radius_m;
  }
  if (!z.rings.empty()) {
    return ring_area_abs(z.rings[0].points);
  }
  return 0.0;
}

void dji_nfz_parse_zone_geometry(const QJsonObject &src, const QString &zone_name,
                                 int fallback_level,
                                 std::vector<DjiNfzZone> *out_zones) {
  if (!out_zones)
    return;

  DjiNfzZone nfz;
  nfz.name = zone_name;
  nfz.level = src.value("level").toInt(fallback_level);
  nfz.color_hex = src.value("color").toString();

  const QJsonValue poly_val = src.value("polygon_points");
  if (poly_val.isArray() && !poly_val.toArray().isEmpty()) {
    nfz.type = DjiNfzType::POLYGON;
    append_polygon_from_value(poly_val, &nfz.rings);

    QJsonValue inner_val = src.value("inner_rings");
    if (!inner_val.isArray())
      inner_val = src.value("holes");
    if (!inner_val.isArray())
      inner_val = src.value("inner_polygons");
    if (inner_val.isArray()) {
      const QJsonArray inner_arr = inner_val.toArray();
      for (int i = 0; i < inner_arr.size(); ++i) {
        DjiPolygonRing inner_ring;
        inner_ring.is_outer = false;
        if (parse_ring_points(inner_arr.at(i), &inner_ring)) {
          nfz.rings.push_back(std::move(inner_ring));
        }
      }
    }

    if (!nfz.rings.empty()) {
      out_zones->push_back(std::move(nfz));
      return;
    }
  }

  const double radius = src.value("radius").toDouble();
  if (radius > 0.0) {
    nfz.type = DjiNfzType::CIRCLE;
    nfz.center_lat = src.value("lat").toDouble();
    nfz.center_lon = src.value("lng").toDouble();
    nfz.radius_m = radius;
    out_zones->push_back(std::move(nfz));
  }
}

void dji_nfz_add_ring_to_path(
    QPainterPath &path, const DjiPolygonRing &ring,
    std::function<bool(double, double, QPoint *)> coord_to_screen_fn,
    bool smooth_edges) {
  if (ring.points.size() < 3)
    return;

  std::vector<QPointF> screen_pts;
  screen_pts.reserve(ring.points.size());
  for (const auto &pt : ring.points) {
    QPoint scr;
    if (coord_to_screen_fn(pt.lat, pt.lon, &scr)) {
      screen_pts.push_back(QPointF(scr));
    }
  }
  if (screen_pts.size() < 3)
    return;

  if (screen_pts.size() >= 4) {
    const QPointF &first = screen_pts.front();
    const QPointF &last = screen_pts.back();
    if (std::abs(first.x() - last.x()) < 0.5 &&
        std::abs(first.y() - last.y()) < 0.5) {
      screen_pts.pop_back();
    }
  }
  if (screen_pts.size() < 3)
    return;

  if (smooth_edges && screen_pts.size() >= 7) {
    const int n = (int)screen_pts.size();
    QPointF p0 = screen_pts[0];
    QPointF p1 = screen_pts[1];
    QPointF start_mid((p0.x() + p1.x()) * 0.5, (p0.y() + p1.y()) * 0.5);
    path.moveTo(start_mid);

    for (int i = 0; i < n; ++i) {
      const QPointF &ctrl = screen_pts[i];
      const QPointF &next = screen_pts[(i + 1) % n];
      QPointF mid((ctrl.x() + next.x()) * 0.5, (ctrl.y() + next.y()) * 0.5);
      path.quadTo(ctrl, mid);
    }
    path.closeSubpath();
    return;
  }

  path.moveTo(screen_pts[0]);
  for (size_t i = 1; i < screen_pts.size(); ++i) {
    path.lineTo(screen_pts[i]);
  }
  path.closeSubpath();
}

bool dji_nfz_looks_like_outer_frame(
    const DjiNfzZone &z, const QRect &panel,
    std::function<bool(double, double, QPoint *)> coord_to_screen_fn) {
  if (z.type != DjiNfzType::POLYGON || z.rings.empty())
    return false;
  const auto &pts = z.rings[0].points;
  if (pts.size() < 4 || pts.size() > 6)
    return false;

  double min_lat = 1e9, max_lat = -1e9;
  double min_lon = 1e9, max_lon = -1e9;
  for (const auto &pt : pts) {
    min_lat = std::min(min_lat, pt.lat);
    max_lat = std::max(max_lat, pt.lat);
    min_lon = std::min(min_lon, pt.lon);
    max_lon = std::max(max_lon, pt.lon);
  }

  QPoint s00, s01, s10, s11;
  if (!coord_to_screen_fn(min_lat, min_lon, &s00) ||
      !coord_to_screen_fn(min_lat, max_lon, &s01) ||
      !coord_to_screen_fn(max_lat, min_lon, &s10) ||
      !coord_to_screen_fn(max_lat, max_lon, &s11)) {
    return false;
  }

  const int min_x = std::min(std::min(s00.x(), s01.x()), std::min(s10.x(), s11.x()));
  const int max_x = std::max(std::max(s00.x(), s01.x()), std::max(s10.x(), s11.x()));
  const int min_y = std::min(std::min(s00.y(), s01.y()), std::min(s10.y(), s11.y()));
  const int max_y = std::max(std::max(s00.y(), s01.y()), std::max(s10.y(), s11.y()));

  const int bw = max_x - min_x;
  const int bh = max_y - min_y;
  const double poly_area = ring_area_abs(pts);
  const double bbox_area = std::abs((max_lat - min_lat) * (max_lon - min_lon));
  const double rectness = (bbox_area > 1e-12) ? (poly_area / bbox_area) : 0.0;

  const QRect expanded_panel = panel.adjusted(-8, -8, 8, 8);
  const bool touch_left = min_x <= expanded_panel.left();
  const bool touch_right = max_x >= expanded_panel.right();
  const bool touch_top = min_y <= expanded_panel.top();
  const bool touch_bottom = max_y >= expanded_panel.bottom();

  const bool rectangle_like = (rectness >= 0.80 && rectness <= 1.10);
  const bool enclosing_mask = touch_left && touch_right && touch_top && touch_bottom;
  const bool wide_strip = touch_left && touch_right &&
                          bh >= (int)std::llround(panel.height() * 0.18);
  const bool tall_strip = touch_top && touch_bottom &&
                          bw >= (int)std::llround(panel.width() * 0.18);

  return rectangle_like && (enclosing_mask || wide_strip || tall_strip);
}
