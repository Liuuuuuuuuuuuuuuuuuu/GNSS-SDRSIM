#include "gui/map/map_osm_panel_utils.h"

#include "gui/map/map_osm_controls_utils.h"
#include "gui/map/map_osm_status_utils.h"
#include "gui/map/map_tile_utils.h"
#include "gui/geo/osm_projection.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool draw_screen_point(const MapOsmPanelInput &in, double lat, double lon,
                       QPoint *out) {
  if (!in.coord_to_screen)
    return false;
  return in.coord_to_screen(lat, lon, out);
}

bool llh_is_valid(double lat_deg, double lon_deg) {
  return std::isfinite(lat_deg) && std::isfinite(lon_deg) &&
         lat_deg >= -90.0 && lat_deg <= 90.0 && lon_deg >= -180.0 &&
         lon_deg <= 180.0;
}

double distance_m_for_display(double lat0_deg, double lon0_deg,
                              double lat1_deg, double lon1_deg) {
  const double lat0 = lat0_deg * M_PI / 180.0;
  const double lat1 = lat1_deg * M_PI / 180.0;
  const double dlat = lat1 - lat0;
  const double dlon = wrap_lon_delta_deg(lon1_deg - lon0_deg) * M_PI / 180.0;
  const double mean_lat = 0.5 * (lat0 + lat1);
  const double x = dlon * std::cos(mean_lat) * 6371000.0;
  const double y = dlat * 6371000.0;
  return std::sqrt(x * x + y * y);
}

double polyline_total_distance_m(const std::vector<LonLat> &polyline) {
  if (polyline.size() < 2)
    return 0.0;
  double total_m = 0.0;
  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    if (!llh_is_valid(a.lat, a.lon) || !llh_is_valid(b.lat, b.lon))
      continue;
    total_m += distance_m_for_display(a.lat, a.lon, b.lat, b.lon);
  }
  return total_m;
}

double polyline_remaining_from_pos_m(const std::vector<LonLat> &polyline,
                                     double pos_lat_deg,
                                     double pos_lon_deg) {
  if (polyline.size() < 2 || !llh_is_valid(pos_lat_deg, pos_lon_deg))
    return std::numeric_limits<double>::quiet_NaN();

  const double kEarthR = 6371000.0;
  std::vector<double> cum_len_m(polyline.size(), 0.0);
  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    double seg_m = 0.0;
    if (llh_is_valid(a.lat, a.lon) && llh_is_valid(b.lat, b.lon)) {
      seg_m = distance_m_for_display(a.lat, a.lon, b.lat, b.lon);
      if (!std::isfinite(seg_m) || seg_m < 0.0)
        seg_m = 0.0;
    }
    cum_len_m[i] = cum_len_m[i - 1] + seg_m;
  }
  const double total_m = cum_len_m.back();
  if (!(total_m > 0.0))
    return std::numeric_limits<double>::quiet_NaN();

  double best_remain_m = std::numeric_limits<double>::infinity();

  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    if (!llh_is_valid(a.lat, a.lon) || !llh_is_valid(b.lat, b.lon))
      continue;

    const double mean_lat_rad =
        0.5 * (a.lat + b.lat) * M_PI / 180.0;
    const double bx = wrap_lon_delta_deg(b.lon - a.lon) * M_PI / 180.0 *
                      std::cos(mean_lat_rad) * kEarthR;
    const double by = (b.lat - a.lat) * M_PI / 180.0 * kEarthR;
    const double seg_len_m = std::sqrt(bx * bx + by * by);
    if (!(seg_len_m > 1e-6)) {
      continue;
    }

    const double px = wrap_lon_delta_deg(pos_lon_deg - a.lon) * M_PI / 180.0 *
                      std::cos(mean_lat_rad) * kEarthR;
    const double py = (pos_lat_deg - a.lat) * M_PI / 180.0 * kEarthR;

    const double dot = px * bx + py * by;
    double t = dot / (seg_len_m * seg_len_m);
    if (t < 0.0)
      t = 0.0;
    if (t > 1.0)
      t = 1.0;

    const double qx = t * bx;
    const double qy = t * by;
    const double dx = px - qx;
    const double dy = py - qy;
    const double cross_track_m = std::sqrt(dx * dx + dy * dy);

    const double along_m = cum_len_m[i - 1] + t * seg_len_m;
    double remain_m = total_m - along_m;
    if (remain_m < 0.0)
      remain_m = 0.0;

    // Prefer on-path projections; allow off-path fallback if user drifts away.
    const double penalty = cross_track_m * 0.02;
    const double scored_remain = remain_m + penalty;
    if (scored_remain < best_remain_m) {
      best_remain_m = remain_m;
    }
  }

  if (!std::isfinite(best_remain_m))
    return std::numeric_limits<double>::quiet_NaN();
  return best_remain_m;
}

double segment_full_distance_m(const MapOsmPanelSegment &seg) {
  if (seg.mode == MapOsmPanelSegment::PathMode::Line) {
    if (!llh_is_valid(seg.start_lat_deg, seg.start_lon_deg) ||
        !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return distance_m_for_display(seg.start_lat_deg, seg.start_lon_deg,
                                  seg.end_lat_deg, seg.end_lon_deg);
  }

  if (seg.polyline.size() >= 2) {
    const double m = polyline_total_distance_m(seg.polyline);
    if (std::isfinite(m) && m >= 0.0)
      return m;
  }

  if (!llh_is_valid(seg.start_lat_deg, seg.start_lon_deg) ||
      !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return distance_m_for_display(seg.start_lat_deg, seg.start_lon_deg,
                                seg.end_lat_deg, seg.end_lon_deg);
}

double segment_remaining_from_receiver_m(const MapOsmPanelSegment &seg,
                                         double rx_lat_deg,
                                         double rx_lon_deg) {
  if (!llh_is_valid(rx_lat_deg, rx_lon_deg) ||
      !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  if (seg.mode == MapOsmPanelSegment::PathMode::Line) {
    return distance_m_for_display(rx_lat_deg, rx_lon_deg, seg.end_lat_deg,
                                  seg.end_lon_deg);
  }

  const double direct_m =
      distance_m_for_display(rx_lat_deg, rx_lon_deg, seg.end_lat_deg,
                             seg.end_lon_deg);

  if (seg.polyline.size() >= 2) {
    const double remain =
        polyline_remaining_from_pos_m(seg.polyline, rx_lat_deg, rx_lon_deg);
    if (std::isfinite(remain) && remain >= 0.0) {
      return std::max(remain, direct_m);
    }
  }

  return direct_m;
}

double preview_full_distance_m(const MapOsmPanelInput &in) {
  if (!llh_is_valid(in.preview_start_lat_deg, in.preview_start_lon_deg) ||
      !llh_is_valid(in.preview_end_lat_deg, in.preview_end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (in.preview_mode == MapOsmPanelSegment::PathMode::Line) {
    return distance_m_for_display(in.preview_start_lat_deg, in.preview_start_lon_deg,
                                  in.preview_end_lat_deg, in.preview_end_lon_deg);
  }
  if (in.preview_polyline && in.preview_polyline->size() >= 2) {
    const double m = polyline_total_distance_m(*in.preview_polyline);
    if (std::isfinite(m) && m >= 0.0)
      return m;
  }
  return distance_m_for_display(in.preview_start_lat_deg, in.preview_start_lon_deg,
                                in.preview_end_lat_deg, in.preview_end_lon_deg);
}

QRect draw_osm_scale_bar(QPainter &p, const MapOsmPanelInput &in) {
  if (in.panel.width() < 120 || in.panel.height() < 120)
    return QRect();

  const double center_lat_deg = osm_world_y_to_lat(in.osm_center_px_y, in.osm_zoom);
  const double world_size_px = osm_world_size_for_zoom(in.osm_zoom);
  const double m_per_px =
      std::cos(center_lat_deg * M_PI / 180.0) * (2.0 * M_PI * 6378137.0) /
      world_size_px;
  if (!std::isfinite(m_per_px) || m_per_px <= 0.0)
    return QRect();

  const double target_px = 120.0;
  const double raw_m = m_per_px * target_px;
  if (!std::isfinite(raw_m) || raw_m <= 0.0)
    return QRect();

  const double p10 = std::pow(10.0, std::floor(std::log10(raw_m)));
  const double candidates[] = {1.0 * p10, 2.0 * p10, 5.0 * p10,
                               10.0 * p10};
  double bar_m = candidates[0];
  for (double c : candidates) {
    if (c <= raw_m)
      bar_m = c;
  }
  if (bar_m <= 0.0)
    return QRect();

  const int bar_px = (int)std::lround(bar_m / m_per_px);
  if (bar_px < 24)
    return QRect();

  QString label;
  if (bar_m >= 1000.0) {
    const double km = bar_m * 0.001;
    int dec = 0;
    if (km < 10.0)
      dec = 2;
    else if (km < 100.0)
      dec = 1;
    label = QString("%1 km").arg(km, 0, 'f', dec);
  } else {
    label = QString("%1 m").arg((int)std::lround(bar_m));
  }

  const int left_pad = 16;
  const int bottom_pad = in.running_ui ? 104 : 78;
  const int base_x = in.panel.x() + left_pad;
  const int base_y = in.panel.y() + in.panel.height() - bottom_pad;

  const QFont old_font = p.font();
  QFont f = old_font;
  f.setPointSize(std::max(9, old_font.pointSize() > 0 ? old_font.pointSize() - 1 : 9));
  f.setBold(in.scale_ruler_enabled);
  p.setFont(f);
  QFontMetrics fm(f);
  const int label_h = fm.height();
  const int label_w = fm.horizontalAdvance(label);

  const QRect bg_rect(base_x - 8, base_y - label_h - 14,
                      std::max(bar_px + 16, label_w + 16), label_h + 18);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(in.scale_ruler_enabled ? QColor(127, 240, 189, 220)
                                       : QColor(180, 200, 220, 185),
                in.scale_ruler_enabled ? 2 : 1));
  p.setBrush(in.scale_ruler_enabled ? QColor(8, 34, 28, 228)
                                    : (in.dark_map_mode ? QColor(7, 15, 26, 210)
                                                        : QColor(9, 20, 35, 210)));
  p.drawRoundedRect(bg_rect, 6, 6);

  const QColor scale_color = in.dark_map_mode ? QColor("#eef4ff")
                                              : QColor("#f4f8ff");
  p.setPen(QPen(scale_color, 2));
  p.drawLine(base_x, base_y - 8, base_x + bar_px, base_y - 8);
  p.drawLine(base_x, base_y - 12, base_x, base_y - 4);
  p.drawLine(base_x + bar_px, base_y - 12, base_x + bar_px, base_y - 4);

  p.setPen(scale_color);
  p.drawText(QRect(base_x, base_y - label_h - 12,
                   std::max(bar_px, label_w), label_h),
             Qt::AlignLeft | Qt::AlignVCenter, label);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(old_font);
  return bg_rect;
}

void draw_scale_ruler_overlay(QPainter &p, const MapOsmPanelInput &in) {
  if (!in.scale_ruler_enabled || !in.scale_ruler_has_start)
    return;

  QPoint a;
  if (!draw_screen_point(in, in.scale_ruler_start_lat_deg,
                         in.scale_ruler_start_lon_deg, &a)) {
    return;
  }

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor("#7ff0bd"), 2));
  p.setBrush(QColor("#7ff0bd"));
  p.drawEllipse(a, 5, 5);

  if (in.scale_ruler_has_end) {
    QPoint b;
    if (draw_screen_point(in, in.scale_ruler_end_lat_deg,
                          in.scale_ruler_end_lon_deg, &b)) {
      QPen line_pen(QColor("#7ff0bd"), 2,
                    in.scale_ruler_end_fixed ? Qt::SolidLine : Qt::DashLine,
                    Qt::RoundCap, Qt::RoundJoin);
      p.setPen(line_pen);
      p.setBrush(Qt::NoBrush);
      p.drawLine(a, b);

      p.setPen(QPen(QColor("#7ff0bd"), 2));
      p.setBrush(QColor("#7ff0bd"));
      p.drawEllipse(b, 5, 5);

      const double d_m = distance_m_for_display(
          in.scale_ruler_start_lat_deg, in.scale_ruler_start_lon_deg,
          in.scale_ruler_end_lat_deg, in.scale_ruler_end_lon_deg);
      if (std::isfinite(d_m) && d_m >= 0.0) {
        const double d_km = d_m * 0.001;
        int dec = 0;
        if (d_km < 10.0)
          dec = 2;
        else if (d_km < 100.0)
          dec = 1;
        QString txt =
          (in.language == GuiLanguage::ZhTw)
                ? QString::fromUtf8("量測 %1 公里").arg(d_km, 0, 'f', dec)
                : QString("RULER %1 km").arg(d_km, 0, 'f', dec);

        QFont old_font = p.font();
        QFont f = old_font;
        f.setPointSize(std::max(9, old_font.pointSize() > 0
                                       ? old_font.pointSize() - 1
                                       : 9));
        p.setFont(f);
        QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(txt) + 14;
        const int h = fm.height() + 8;
        const QPoint m((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
        QRect tag(m.x() - w / 2, m.y() - h - 8, w, h);
        tag.translate(0, -4);
        tag = tag.intersected(in.panel.adjusted(6, 6, -6, -6));

        p.setPen(QPen(QColor(127, 240, 189, 220), 1));
        p.setBrush(QColor(8, 30, 24, 220));
        p.drawRoundedRect(tag, 6, 6);
        p.setPen(QColor("#dffff2"));
        p.drawText(tag, Qt::AlignCenter, txt);
        p.setFont(old_font);
      }
    }
  }
  p.setRenderHint(QPainter::Antialiasing, false);
}

} // namespace

void map_draw_osm_panel(QPainter &p, const MapOsmPanelInput &in,
                        MapOsmPanelState *out) {
  map_draw_osm_panel_background(p, in);
  map_draw_osm_panel_overlay(p, in, out);
}

void map_draw_osm_panel_background(QPainter &p, const MapOsmPanelInput &in) {
  if (in.panel.width() < 8 || in.panel.height() < 8)
    return;

  QColor color_bg_top(in.dark_map_mode ? "#0b1a2b" : "#0e2239");
  QColor color_bg_bottom(in.dark_map_mode ? "#06101d" : "#081425");
  QColor color_border("#c6d4e6");

  QRect shell = in.panel.adjusted(0, 0, -1, -1);
  const int corner = 10;
  QPainterPath panel_path;
  panel_path.addRoundedRect(QRectF(shell), corner, corner);

  QLinearGradient shell_grad(shell.topLeft(), shell.bottomLeft());
  shell_grad.setColorAt(0.0, color_bg_top);
  shell_grad.setColorAt(1.0, color_bg_bottom);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(color_border, 1));
  p.setBrush(shell_grad);
  p.drawPath(panel_path);
  p.setRenderHint(QPainter::Antialiasing, false);

  // Clip tiles to the rounded panel path so the map fills the full panel again.
  p.save();
  p.setClipPath(panel_path);

  MapTileRange range = map_tile_visible_range(
      in.osm_zoom, in.osm_center_px_x, in.osm_center_px_y, in.panel.width(),
      in.panel.height());

  for (int ty = range.ty0; ty <= range.ty1; ++ty) {
    if (ty < 0 || ty >= range.n)
      continue;
    for (int tx = range.tx0; tx <= range.tx1; ++tx) {
      int tx_wrap = map_tile_wrap_x(tx, range.n);
      double world_x = (double)tx * 256.0;
      double world_y = (double)ty * 256.0;
      int sx = in.panel.x() + (int)llround(world_x - range.left);
      int sy = in.panel.y() + (int)llround(world_y - range.top);

      QString key = map_tile_key(in.osm_zoom, tx_wrap, ty, in.dark_map_mode);
      if (in.tile_cache) {
        auto it = in.tile_cache->find(key);
        if (it != in.tile_cache->end()) {
          p.drawPixmap(sx, sy, 256, 256, it->second);
          continue;
        }
      }
      // Missing tile: fill without border so no grid lines show.
      p.fillRect(QRect(sx, sy, 256, 256),
                 in.dark_map_mode ? QColor(10, 23, 40) : QColor(14, 32, 54));
    }
  }

  p.restore();
}

void map_draw_osm_panel_overlay(QPainter &p, const MapOsmPanelInput &in,
                                MapOsmPanelState *out) {
  if (out) {
    out->lang_btn_rect = QRect();
    out->back_btn_rect = QRect();
    out->nfz_btn_rect = QRect();
    out->dark_mode_btn_rect = QRect();
    out->tutorial_toggle_rect = QRect();
    out->search_return_btn_rect = QRect();
    out->osm_stop_btn_rect = QRect();
    out->osm_runtime_rect = QRect();
    out->osm_scale_bar_rect = QRect();
    out->status_badge_rects.clear();
  }

  if (in.nfz_enabled && in.nfz_zones && !in.nfz_zones->empty()) {
    dji_nfz_draw(p, in.panel, *in.nfz_zones, in.osm_zoom,
                 [&](double lat, double lon, QPoint *out_pt) {
                   return draw_screen_point(in, lat, lon, out_pt);
                 });
  }

  if (in.has_selected_llh) {
    double world = osm_world_size_for_zoom(in.osm_zoom);
    double left = in.osm_center_px_x - (double)in.panel.width() * 0.5;
    double top = in.osm_center_px_y - (double)in.panel.height() * 0.5;
    double wx0 = osm_lon_to_world_x(in.selected_lon_deg, in.osm_zoom);
    double wy = osm_lat_to_world_y(in.selected_lat_deg, in.osm_zoom);

    int best_sx = 0;
    int best_sy = 0;
    double best_dist = 1e30;
    bool visible = false;
    for (int k = -1; k <= 1; ++k) {
      double wx = wx0 + (double)k * world;
      int sx = in.panel.x() + (int)llround(wx - left);
      int sy = in.panel.y() + (int)llround(wy - top);
      if (sx >= in.panel.x() - 12 && sx <= in.panel.x() + in.panel.width() + 12 &&
          sy >= in.panel.y() - 12 && sy <= in.panel.y() + in.panel.height() + 12) {
        visible = true;
        double dx = (double)sx - (in.panel.x() + in.panel.width() * 0.5);
        double dy = (double)sy - (in.panel.y() + in.panel.height() * 0.5);
        double dist = dx * dx + dy * dy;
        if (dist < best_dist) {
          best_dist = dist;
          best_sx = sx;
          best_sy = sy;
        }
      }
    }

    if (visible) {
      p.setRenderHint(QPainter::Antialiasing, true);
      QColor target_color("#00ffcc");
      p.setPen(QPen(target_color, 2));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(QPoint(best_sx, best_sy), 10, 10);
      p.drawLine(best_sx - 16, best_sy, best_sx - 5, best_sy);
      p.drawLine(best_sx + 5, best_sy, best_sx + 16, best_sy);
      p.drawLine(best_sx, best_sy - 16, best_sx, best_sy - 5);
      p.drawLine(best_sx, best_sy + 5, best_sx, best_sy + 16);
      p.setBrush(target_color);
      p.drawEllipse(QPoint(best_sx, best_sy), 2, 2);
      p.setRenderHint(QPainter::Antialiasing, false);
    }
  }

  static const std::vector<MapOsmPanelSegment> kEmptySegments;
  const std::vector<MapOsmPanelSegment> &segs =
      in.path_segments ? *in.path_segments : kEmptySegments;

  for (const auto &seg : segs) {
    QColor c = (seg.state == MapOsmPanelSegment::SegmentState::Executing)
                   ? QColor("#f59e0b")
                   : QColor("#22c55e");
    p.setPen(QPen(c, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    bool has_path = false;
    if (seg.polyline.size() >= 2) {
      for (size_t i = 0; i < seg.polyline.size(); ++i) {
        QPoint pt;
        if (!draw_screen_point(in, seg.polyline[i].lat, seg.polyline[i].lon, &pt))
          continue;
        if (!has_path) {
          path.moveTo(pt);
          has_path = true;
        } else {
          path.lineTo(pt);
        }
      }
    }
    if (!has_path) {
      QPoint a, b;
      if (draw_screen_point(in, seg.start_lat_deg, seg.start_lon_deg, &a) &&
          draw_screen_point(in, seg.end_lat_deg, seg.end_lon_deg, &b)) {
        path.moveTo(a);
        path.lineTo(b);
        has_path = true;
      }
    }
    if (has_path)
      p.drawPath(path);

    QPoint a, b;
    if (!draw_screen_point(in, seg.start_lat_deg, seg.start_lon_deg, &a))
      continue;
    if (!draw_screen_point(in, seg.end_lat_deg, seg.end_lon_deg, &b))
      continue;
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawEllipse(a, 6, 6);
    p.drawEllipse(b, 6, 6);
  }

  if (in.has_preview_segment) {
    QPoint a, b;
    if (draw_screen_point(in, in.preview_start_lat_deg, in.preview_start_lon_deg, &a) &&
        draw_screen_point(in, in.preview_end_lat_deg, in.preview_end_lon_deg, &b)) {
      QColor c = (in.preview_mode == MapOsmPanelSegment::PathMode::Line)
             ? QColor("#60a5fa")
             : QColor("#fbbf24");
      p.setPen(QPen(c, 4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
      p.setBrush(Qt::NoBrush);

      QPainterPath path;
      bool has_path = false;
      if (in.preview_polyline && in.preview_polyline->size() >= 2) {
        for (size_t i = 0; i < in.preview_polyline->size(); ++i) {
          QPoint pt;
          if (!draw_screen_point(in, (*in.preview_polyline)[i].lat,
                                 (*in.preview_polyline)[i].lon, &pt))
            continue;
          if (!has_path) {
            path.moveTo(pt);
            has_path = true;
          } else {
            path.lineTo(pt);
          }
        }
      }
      if (!has_path) {
        path.moveTo(a);
        path.lineTo(b);
        has_path = true;
      }
      if (has_path)
        p.drawPath(path);

      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawEllipse(a, 5, 5);
      p.drawEllipse(b, 5, 5);
    }
  }

  if (in.receiver_valid) {
    QPoint cur;
    if (draw_screen_point(in, in.receiver_lat_deg, in.receiver_lon_deg, &cur)) {
      QColor cur_color("#22d3ee");
      QColor cur_outline("#0f172a");
      p.setPen(QPen(cur_outline, 2));
      p.setBrush(cur_color);
      p.drawEllipse(cur, 8, 8);
      p.drawLine(cur.x() - 12, cur.y(), cur.x() + 12, cur.y());
      p.drawLine(cur.x(), cur.y() - 12, cur.x(), cur.y() + 12);
    }
  }

  const QRect scale_bar_rect = draw_osm_scale_bar(p, in);
  draw_scale_ruler_overlay(p, in);

  long long elapsed_sec = in.running_ui ? in.elapsed_sec : 0;
  bool show_target_distance = false;
  double target_distance_km = 0.0;
  if (in.running_ui && in.receiver_valid &&
      llh_is_valid(in.receiver_lat_deg, in.receiver_lon_deg)) {
    double remain_m = 0.0;
    bool has_remain = false;

    int exec_idx = -1;
    for (size_t i = 0; i < segs.size(); ++i) {
      if (segs[i].state == MapOsmPanelSegment::SegmentState::Executing) {
        exec_idx = (int)i;
        break;
      }
    }

    if (exec_idx >= 0) {
      const double exec_remain = segment_remaining_from_receiver_m(
          segs[(size_t)exec_idx], in.receiver_lat_deg, in.receiver_lon_deg);
      if (std::isfinite(exec_remain) && exec_remain >= 0.0) {
        remain_m += exec_remain;
        has_remain = true;
      }
      for (size_t i = (size_t)exec_idx + 1; i < segs.size(); ++i) {
        const double seg_full = segment_full_distance_m(segs[i]);
        if (std::isfinite(seg_full) && seg_full >= 0.0) {
          remain_m += seg_full;
          has_remain = true;
        }
      }
    } else if (!segs.empty()) {
      const double first_remain = segment_remaining_from_receiver_m(
          segs.front(), in.receiver_lat_deg, in.receiver_lon_deg);
      if (std::isfinite(first_remain) && first_remain >= 0.0) {
        remain_m += first_remain;
        has_remain = true;
      }
      for (size_t i = 1; i < segs.size(); ++i) {
        const double seg_full = segment_full_distance_m(segs[i]);
        if (std::isfinite(seg_full) && seg_full >= 0.0) {
          remain_m += seg_full;
          has_remain = true;
        }
      }
    }

    if (in.has_preview_segment &&
        llh_is_valid(in.preview_end_lat_deg, in.preview_end_lon_deg)) {
      double preview_m = std::numeric_limits<double>::quiet_NaN();
      if (has_remain) {
        preview_m = preview_full_distance_m(in);
      } else {
        // Preview distance should represent anchor->target real route length,
        // not a nearest-point projection that can under-report on looped roads.
        preview_m = preview_full_distance_m(in);
      }
      if (std::isfinite(preview_m) && preview_m >= 0.0) {
        remain_m += preview_m;
        has_remain = true;
      }
    }

    if (has_remain && std::isfinite(remain_m) && remain_m >= 0.0 &&
        remain_m <= 20000000.0) {
      show_target_distance = true;
      target_distance_km = remain_m * 0.001;
    }
  }

  MapOsmControlsInput controls_in;
  controls_in.panel = in.panel;
  controls_in.language = in.language;
  controls_in.running_ui = in.running_ui;
  controls_in.can_undo = in.can_undo;
  controls_in.dji_on = in.nfz_enabled;
  controls_in.dark_map_mode = in.dark_map_mode;
  controls_in.tutorial_enabled = in.tutorial_enabled;
  controls_in.tutorial_hovered = in.tutorial_hovered;
  controls_in.tutorial_overlay_visible = in.tutorial_overlay_visible;
  controls_in.tutorial_step = in.tutorial_step;
  controls_in.show_search_return = in.show_search_return;
  controls_in.search_box_rect = in.search_box_rect;
  controls_in.nfz_layer_visible = in.nfz_layer_visible;
  controls_in.tx_active = in.tx_active;
  controls_in.elapsed_sec = elapsed_sec;
  controls_in.show_target_distance = show_target_distance;
  controls_in.target_distance_km = target_distance_km;
  controls_in.force_stop_preview = in.force_stop_preview;

  MapOsmControlsState controls_out;
  map_osm_draw_controls(p, controls_in, &controls_out);
  if (out) {
    out->lang_btn_rect = controls_out.lang_btn_rect;
    out->back_btn_rect = controls_out.back_btn_rect;
    out->nfz_btn_rect = controls_out.nfz_btn_rect;
    out->dark_mode_btn_rect = controls_out.dark_mode_btn_rect;
    out->tutorial_toggle_rect = controls_out.tutorial_toggle_rect;
    out->search_return_btn_rect = controls_out.search_return_btn_rect;
    out->osm_stop_btn_rect = controls_out.osm_stop_btn_rect;
    out->osm_runtime_rect = controls_out.osm_runtime_rect;
    out->osm_scale_bar_rect = scale_bar_rect;
    out->nfz_legend_row_rects = controls_out.nfz_legend_row_rects;
  }

  QString status_txt = in.plan_status;
  QString cur_txt = map_osm_current_text(in.receiver_valid, in.receiver_lat_deg,
                                         in.receiver_lon_deg, in.language);
  const double zoom_factor =
      std::pow(2.0, (double)in.osm_zoom - (double)in.osm_zoom_base);
  QString llh_txt = map_osm_llh_text(in.has_selected_llh, in.selected_lat_deg,
                                     in.selected_lon_deg, 0.0, cur_txt,
                                     in.language);
  llh_txt = QString("%1 | zoom %2X")
                .arg(llh_txt)
                .arg(zoom_factor, 0, 'f', 2);
  QString zoom_txt = map_osm_zoom_text(in.osm_zoom, in.osm_zoom_base,
                                       in.language);
  QStringList lines = map_osm_status_lines(zoom_txt, in.tutorial_enabled,
                                           in.tutorial_overlay_visible,
                                           status_txt, llh_txt, in.language);
  std::vector<QRect> badge_rects;
  map_osm_draw_status_badges(p, in.panel, controls_out.osm_stop_btn_rect,
                             in.running_ui, lines, &badge_rects);
  if (out) {
    out->status_badge_rects = std::move(badge_rects);
  }

  if (in.jam_selected) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath dim_path;
    dim_path.addRoundedRect(QRectF(in.panel.adjusted(1, 1, -1, -1)), 10, 10);

    QPainterPath keep_clear;
    if (!controls_out.osm_stop_btn_rect.isEmpty()) {
      const QRect stop_keep = controls_out.osm_stop_btn_rect.adjusted(-8, -6, 8, 6);
      keep_clear.addRoundedRect(QRectF(stop_keep), 10, 10);
    }
    if (!controls_out.osm_runtime_rect.isEmpty()) {
      const QRect runtime_keep = controls_out.osm_runtime_rect.adjusted(-8, -6, 8, 6);
      keep_clear.addRoundedRect(QRectF(runtime_keep), 10, 10);
    }
    if (!controls_out.lang_btn_rect.isEmpty()) {
      const QRect lang_keep = controls_out.lang_btn_rect.adjusted(-6, -4, 6, 4);
      keep_clear.addRoundedRect(QRectF(lang_keep), 8, 8);
    }
    if (!controls_out.tutorial_toggle_rect.isEmpty()) {
      const QRect bulb_keep = controls_out.tutorial_toggle_rect.adjusted(-6, -4, 6, 4);
      keep_clear.addRoundedRect(QRectF(bulb_keep), 8, 8);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(118, 126, 136, 88));
    p.drawPath(dim_path.subtracted(keep_clear));
    p.setRenderHint(QPainter::Antialiasing, false);
  }
}