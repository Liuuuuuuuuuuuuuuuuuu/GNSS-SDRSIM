#ifndef MAP_OSM_CONTROLS_UTILS_H
#define MAP_OSM_CONTROLS_UTILS_H

#include <QPainter>
#include <QRect>

#include "gui/core/gui_i18n.h"

struct MapOsmControlsInput {
  QRect panel;
  GuiLanguage language = GuiLanguage::English;
  bool running_ui = false;
  bool can_undo = false;
  bool dji_on = false;
  bool dark_map_mode = false;
  bool tutorial_enabled = false;
  bool tutorial_overlay_visible = false;
  int tutorial_step = 0;
  bool show_search_return = false;
  QRect search_box_rect;
  bool tx_active = false;
  long long elapsed_sec = 0;
    bool force_stop_preview = false;
};

struct MapOsmControlsState {
  QRect lang_btn_rect;
  QRect back_btn_rect;
  QRect nfz_btn_rect;
  QRect dark_mode_btn_rect;
  QRect tutorial_toggle_rect;
  QRect search_return_btn_rect;
  QRect osm_stop_btn_rect;
  QRect osm_runtime_rect;
  std::vector<QRect> nfz_legend_row_rects;
};

void map_osm_draw_controls(QPainter &p, const MapOsmControlsInput &in,
                           MapOsmControlsState *out);

#endif