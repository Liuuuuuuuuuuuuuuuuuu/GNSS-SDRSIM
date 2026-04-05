#ifndef MAP_HOVER_UTILS_H
#define MAP_HOVER_UTILS_H

#include <QPainter>
#include <QPoint>
#include <QRect>

#include "gui/core/control_state.h"

struct MapHoverHelpInput {
  int win_width = 0;
  int win_height = 0;
  bool tutorial_overlay_visible = false;
  bool dark_map_mode = false;
  bool show_search_return = false;
  bool search_box_visible = false;
  QRect search_box_rect;
  QRect search_return_btn_rect;
  QRect dark_mode_btn_rect;
  QRect nfz_btn_rect;
  QRect tutorial_toggle_rect;
  QRect back_btn_rect;
  QRect osm_stop_btn_rect;
  QRect osm_runtime_rect;
  QRect osm_panel_rect;
  GuiControlState ctrl;
  bool nfz_on = false;
};

int map_hover_region_for_pos(const QPoint &pos, const MapHoverHelpInput &in,
                             QString *text, QRect *anchor);
void map_draw_hover_help_overlay(QPainter &p, const MapHoverHelpInput &in,
                                 bool hover_visible, const QString &hover_text,
                                 const QRect &hover_anchor);

#endif