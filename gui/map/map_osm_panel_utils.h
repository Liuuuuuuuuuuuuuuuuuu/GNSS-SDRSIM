#ifndef MAP_OSM_PANEL_UTILS_H
#define MAP_OSM_PANEL_UTILS_H

#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>

#include <array>
#include <functional>
#include <unordered_map>
#include <vector>

#include "gui/nfz/dji_nfz.h"
#include "gui/geo/geo_io.h"
#include "gui/core/gui_i18n.h"

struct MapOsmPanelSegment {
  double start_lat_deg = 0.0;
  double start_lon_deg = 0.0;
  double end_lat_deg = 0.0;
  double end_lon_deg = 0.0;
  enum class PathMode : int { Plan = 0, Line = 1 };
  enum class SegmentState : int { Queued = 0, Executing = 1 };
  PathMode mode = PathMode::Plan;
  SegmentState state = SegmentState::Queued;
  std::vector<LonLat> polyline;
};

inline constexpr MapOsmPanelSegment::PathMode
map_osm_panel_path_mode_from_int(int mode) {
  return (mode == static_cast<int>(MapOsmPanelSegment::PathMode::Line))
             ? MapOsmPanelSegment::PathMode::Line
             : MapOsmPanelSegment::PathMode::Plan;
}

inline constexpr MapOsmPanelSegment::SegmentState
map_osm_panel_segment_state_from_int(int state) {
  return (state == static_cast<int>(MapOsmPanelSegment::SegmentState::Executing))
             ? MapOsmPanelSegment::SegmentState::Executing
             : MapOsmPanelSegment::SegmentState::Queued;
}

struct MapOsmPanelInput {
  QRect panel;
  GuiLanguage language = GuiLanguage::English;
  int osm_zoom = 0;
  int osm_zoom_base = 0;
  double osm_center_px_x = 0.0;
  double osm_center_px_y = 0.0;
  const std::unordered_map<QString, QPixmap> *tile_cache = nullptr;
  const std::vector<MapOsmPanelSegment> *path_segments = nullptr;
  const std::vector<LonLat> *preview_polyline = nullptr;
  const std::vector<DjiNfzZone> *nfz_zones = nullptr;
  std::function<bool(double lat, double lon, QPoint *out)> coord_to_screen;
  bool nfz_enabled = false;
  bool has_selected_llh = false;
  double selected_lat_deg = 0.0;
  double selected_lon_deg = 0.0;
  bool has_preview_segment = false;
  MapOsmPanelSegment::PathMode preview_mode =
      MapOsmPanelSegment::PathMode::Plan;
  double preview_start_lat_deg = 0.0;
  double preview_start_lon_deg = 0.0;
  double preview_end_lat_deg = 0.0;
  double preview_end_lon_deg = 0.0;
  bool receiver_valid = false;
  double receiver_lat_deg = 0.0;
  double receiver_lon_deg = 0.0;
  bool running_ui = false;
  bool jam_selected = false;
  bool can_undo = false;
  bool dark_map_mode = false;
  bool tutorial_enabled = false;
  bool tutorial_hovered = false;
  bool tutorial_overlay_visible = false;
  int tutorial_step = 0;
  bool show_search_return = false;
  QRect search_box_rect;
  std::array<bool, 4> nfz_layer_visible = {true, false, false, false};
  bool tx_active = false;
  long long elapsed_sec = 0;
  QString plan_status;
    bool force_stop_preview = false;
};

struct MapOsmPanelState {
  QRect lang_btn_rect;
  QRect back_btn_rect;
  QRect nfz_btn_rect;
  QRect dark_mode_btn_rect;
  QRect tutorial_toggle_rect;
  QRect search_return_btn_rect;
  QRect osm_stop_btn_rect;
  QRect osm_runtime_rect;
  std::vector<QRect> status_badge_rects;
  std::vector<QRect> nfz_legend_row_rects;
};

void map_draw_osm_panel(QPainter &p, const MapOsmPanelInput &in,
                        MapOsmPanelState *out);
void map_draw_osm_panel_background(QPainter &p, const MapOsmPanelInput &in);
void map_draw_osm_panel_overlay(QPainter &p, const MapOsmPanelInput &in,
                                MapOsmPanelState *out);

#endif