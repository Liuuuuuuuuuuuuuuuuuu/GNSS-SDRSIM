#include "gui/map/map_osm_panel_utils.h"

#include "gui/map/map_osm_controls_utils.h"
#include "gui/map/map_osm_status_utils.h"
#include "gui/map/map_tile_utils.h"
#include "gui/geo/osm_projection.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {

bool draw_screen_point(const MapOsmPanelInput &in, double lat, double lon,
                       QPoint *out) {
  if (!in.coord_to_screen)
    return false;
  return in.coord_to_screen(lat, lon, out);
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
  QLinearGradient shell_grad(shell.topLeft(), shell.bottomLeft());
  shell_grad.setColorAt(0.0, color_bg_top);
  shell_grad.setColorAt(1.0, color_bg_bottom);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(color_border, 1));
  p.setBrush(shell_grad);
  p.drawRoundedRect(shell, 10, 10);
  p.setRenderHint(QPainter::Antialiasing, false);

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
      p.fillRect(QRect(sx, sy, 256, 256),
                 in.dark_map_mode ? QColor(10, 23, 40) : QColor(14, 32, 54));
      p.setPen(QPen(QColor(74, 101, 132), 1));
      p.drawRect(sx, sy, 255, 255);
    }
  }
}

void map_draw_osm_panel_overlay(QPainter &p, const MapOsmPanelInput &in,
                                MapOsmPanelState *out) {
  if (out) {
    out->back_btn_rect = QRect();
    out->nfz_btn_rect = QRect();
    out->dark_mode_btn_rect = QRect();
    out->tutorial_toggle_rect = QRect();
    out->search_return_btn_rect = QRect();
    out->osm_stop_btn_rect = QRect();
    out->osm_runtime_rect = QRect();
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

  std::vector<MapOsmPanelSegment> segs;
  if (in.path_segments)
    segs = *in.path_segments;

  for (const auto &seg : segs) {
    QColor c = (seg.state == 1) ? QColor("#f59e0b") : QColor("#22c55e");
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
      QColor c = (in.preview_mode == 1) ? QColor("#60a5fa") : QColor("#fbbf24");
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

  if (in.jam_selected) {
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(in.panel.adjusted(2, 2, -2, -2), QColor(0, 0, 0, 120));
  }

  long long elapsed_sec = in.running_ui ? in.elapsed_sec : 0;
  MapOsmControlsInput controls_in;
  controls_in.panel = in.panel;
  controls_in.running_ui = in.running_ui;
  controls_in.can_undo = in.can_undo;
  controls_in.dji_on = in.nfz_enabled;
  controls_in.dark_map_mode = in.dark_map_mode;
  controls_in.tutorial_enabled = in.tutorial_enabled;
  controls_in.tutorial_overlay_visible = in.tutorial_overlay_visible;
  controls_in.tutorial_step = in.tutorial_step;
  controls_in.show_search_return = in.show_search_return;
  controls_in.search_box_rect = in.search_box_rect;
  controls_in.tx_active = in.tx_active;
  controls_in.elapsed_sec = elapsed_sec;

  MapOsmControlsState controls_out;
  map_osm_draw_controls(p, controls_in, &controls_out);
  if (out) {
    out->back_btn_rect = controls_out.back_btn_rect;
    out->nfz_btn_rect = controls_out.nfz_btn_rect;
    out->dark_mode_btn_rect = controls_out.dark_mode_btn_rect;
    out->tutorial_toggle_rect = controls_out.tutorial_toggle_rect;
    out->search_return_btn_rect = controls_out.search_return_btn_rect;
    out->osm_stop_btn_rect = controls_out.osm_stop_btn_rect;
    out->osm_runtime_rect = controls_out.osm_runtime_rect;
  }

  QString status_txt = in.plan_status;
  QString cur_txt = map_osm_current_text(in.receiver_valid, in.receiver_lat_deg,
                                         in.receiver_lon_deg);
  QString llh_txt = map_osm_llh_text(in.has_selected_llh, in.selected_lat_deg,
                                     in.selected_lon_deg, 0.0, cur_txt);
  QString zoom_txt = map_osm_zoom_text(in.osm_zoom, in.osm_zoom_base);
  QStringList lines = map_osm_status_lines(zoom_txt, in.tutorial_enabled,
                                           in.tutorial_overlay_visible,
                                           status_txt, llh_txt);
  map_osm_draw_status_badges(p, in.panel, controls_out.osm_stop_btn_rect,
                             in.running_ui, lines);
}