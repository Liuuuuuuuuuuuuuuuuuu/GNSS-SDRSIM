#ifndef MAP_OSM_CONTROLS_UTILS_H
#define MAP_OSM_CONTROLS_UTILS_H

#include <QPainter>
#include <QRect>

#include <array>

#include "gui/core/i18n/gui_i18n.h"

struct MapOsmControlsInput {
  QRect panel;
  GuiLanguage language = GuiLanguage::English;
  bool running_ui = false;
  bool receiver_valid = false;
  double receiver_lat_deg = 0.0;
  double receiver_lon_deg = 0.0;
  bool can_undo = false;
  bool dji_on = false;
  bool show_range_cap_legend = false;
  bool dark_map_mode = false;
  bool tutorial_enabled = false;
  bool tutorial_hovered = false;
  bool show_search_return = false;
  QRect search_box_rect;
  bool show_launch_button = false;
  bool tx_active = false;
  long long elapsed_sec = 0;
  bool show_target_distance = false;
  double target_distance_km = 0.0;
  std::array<bool, 4> nfz_layers_rendered = {false, false, false, false};
};

struct MapOsmControlsState {
  QRect lang_btn_rect;
  QRect back_btn_rect;
  QRect recenter_btn_rect;
  QRect nfz_btn_rect;
  QRect dark_mode_btn_rect;
  QRect tutorial_toggle_rect;
  QRect search_return_btn_rect;
  QRect osm_stop_btn_rect;
  QRect osm_launch_btn_rect;
  QRect osm_runtime_rect;
  std::vector<QRect> nfz_legend_row_rects;
};

void map_osm_draw_controls(QPainter &p, const MapOsmControlsInput &in,
                           MapOsmControlsState *out);

#endif