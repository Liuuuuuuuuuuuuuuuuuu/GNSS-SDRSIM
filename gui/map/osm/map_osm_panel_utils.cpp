#include "gui/map/osm/map_osm_panel_utils.h"

#include "gui/map/osm/map_osm_controls_utils.h"
#include "gui/map/osm/map_osm_metrics_utils.h"
#include "gui/map/osm/map_osm_scale_ruler_utils.h"
#include "gui/map/osm/map_osm_status_utils.h"
#include "gui/layout/geometry/control_layout.h"
#include "gui/control/panel/control_paint.h"
#include "gui/map/osm/map_tile_utils.h"
#include "gui/geo/osm_projection.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void draw_focus_marker(QPainter &p, const QPoint &pt, const QColor &fill,
                       const QColor &outline, double radius_px,
                       double halo_radius_px) {
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(fill.red(), fill.green(), fill.blue(), 60));
  p.drawEllipse(QPointF(pt), halo_radius_px, halo_radius_px);
  p.setPen(QPen(outline, 2));
  p.setBrush(fill);
  p.drawEllipse(QPointF(pt), radius_px, radius_px);
  p.setRenderHint(QPainter::Antialiasing, false);
}

bool draw_screen_point(const MapOsmPanelInput &in, double lat, double lon,
                       QPoint *out) {
  if (!in.coord_to_screen)
    return false;
  return in.coord_to_screen(lat, lon, out);
}

bool llh_is_valid(double lat_deg, double lon_deg) {
  return map_osm_metrics::llh_is_valid(lat_deg, lon_deg);
}

double segment_full_distance_m(const MapOsmPanelSegment &seg) {
  return map_osm_metrics::segment_full_distance_m(seg);
}

double segment_remaining_from_receiver_m(const MapOsmPanelSegment &seg,
                                         double rx_lat_deg,
                                         double rx_lon_deg) {
  return map_osm_metrics::segment_remaining_from_receiver_m(seg,
                                                            rx_lat_deg,
                                                            rx_lon_deg);
}

double preview_full_distance_m(const MapOsmPanelInput &in) {
  return map_osm_metrics::preview_full_distance_m(in);
}

bool draw_parent_tile_fallback(QPainter &p, const MapOsmPanelInput &in,
                               int zoom, int tx_wrap, int ty,
                               int sx, int sy) {
  if (!in.tile_cache)
    return false;

  for (int dz = 1; dz <= 3; ++dz) {
    const int parent_zoom = zoom - dz;
    if (parent_zoom < 0)
      break;

    const int stride = 1 << dz;
    const int parent_n = 1 << parent_zoom;
    const int parent_tx = map_tile_wrap_x(tx_wrap / stride, parent_n);
    const int parent_ty = ty / stride;
    if (parent_ty < 0 || parent_ty >= parent_n)
      continue;

    const QString parent_key =
        map_tile_key(parent_zoom, parent_tx, parent_ty, in.dark_map_mode);
    auto it = in.tile_cache->find(parent_key);
    if (it == in.tile_cache->end())
      continue;

    const int child_mask = stride - 1;
    const int sub_x = tx_wrap & child_mask;
    const int sub_y = ty & child_mask;
    const int src_size = 256 / stride;
    const QRect src(sub_x * src_size, sub_y * src_size, src_size, src_size);
    p.drawPixmap(QRect(sx, sy, 256, 256), it->second, src);
    return true;
  }

  return false;
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
  // Disable smooth-pixmap interpolation to prevent sub-pixel darkening at tile
  // boundaries which shows up as visible 1-pixel grid seam lines on the map.
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);

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
      if (draw_parent_tile_fallback(p, in, in.osm_zoom, tx_wrap, ty, sx, sy)) {
        continue;
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
      draw_focus_marker(p, QPoint(best_sx, best_sy), QColor("#00ffcc"),
                        QColor(245, 255, 252, 235), 6.0, 13.0);
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

  const QRect scale_bar_rect = map_osm_draw_scale_bar(p, in);
  map_osm_draw_scale_ruler_overlay(p, in);

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
  controls_in.show_range_cap_legend =
      in.running_ui && in.has_selected_llh && !in.jam_selected;
  controls_in.dark_map_mode = in.dark_map_mode;
  controls_in.tutorial_enabled = in.tutorial_enabled;
  controls_in.tutorial_hovered = in.tutorial_hovered;
  controls_in.show_search_return = in.show_search_return;
  controls_in.search_box_rect = in.search_box_rect;
  controls_in.tx_active = in.tx_active;
  controls_in.elapsed_sec = elapsed_sec;
  controls_in.show_target_distance = show_target_distance;
  controls_in.target_distance_km = target_distance_km;

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

  QString cur_txt;
  if (in.receiver_valid) {
    cur_txt = gui_i18n_text(in.language, "status.current")
                  .arg(in.receiver_lat_deg, 0, 'f', 6)
                  .arg(in.receiver_lon_deg, 0, 'f', 6);
  } else {
    cur_txt = gui_i18n_text(in.language, "status.current_na");
  }
  const double zoom_factor =
      std::pow(2.0, (double)in.osm_zoom - (double)in.osm_zoom_base);
  QString llh_txt = cur_txt;
  llh_txt = QString("%1 | zoom %2X")
                .arg(llh_txt)
                .arg(zoom_factor, 0, 'f', 2);
  QStringList lines;
  if (!llh_txt.isEmpty()) {
    lines << llh_txt;
  }
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

void map_osm_redraw_stop_btn(QPainter &p, const QRect &btn_rect,
                             GuiLanguage language) {
  if (btn_rect.isEmpty())
    return;
  const QColor btn_stop("#ef5350");
  const QByteArray label_ba = gui_i18n_text(language, "osm.stop").toUtf8();
  const Rect rr{btn_rect.x(), btn_rect.y(), btn_rect.width(), btn_rect.height()};
  control_draw_button_filled(p, rr, btn_stop, btn_stop, QColor(8, 12, 18),
                             label_ba.constData());
}