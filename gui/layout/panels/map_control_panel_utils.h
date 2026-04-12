#ifndef MAP_CONTROL_PANEL_UTILS_H
#define MAP_CONTROL_PANEL_UTILS_H

#include <QPainter>
#include <QString>
#include <QColor>

#include <vector>

#include "gui/core/state/control_state.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/state/signal_snapshot.h"

struct DroneCenterListItem {
  QString device_id;
  QString model;
  double confidence = 0.0;
};

struct DroneCenterRadarPoint {
  double bearing_deg = 0.0;
  double distance_m = 0.0;
  double speed_mps = 0.0;
  bool has_speed = false;
  bool hostile = true;
  bool is_ble_rid = false;
  bool is_wifi_rid = false;
};

struct MapControlPanelInput {
  GuiControlState ctrl;
  TimeInfo time_info;
  QString rnx_name_bds;
  QString rnx_name_gps;
  GuiLanguage language = GuiLanguage::English;
  double control_text_scale = 1.0;
  double caption_text_scale = 1.0;
  double switch_option_text_scale = 1.0;
  double value_text_scale = 1.0;
  QColor accent_color = QColor("#00e5ff");
  QColor border_color = QColor("#b9cadf");
  QColor text_color = QColor("#f8fbff");
  QColor dim_text_color = QColor("#6b7b90");

  bool show_drone_center = false;
  int unknown_signal_count = 0;
  int whitelist_count = 0;
  int confirmed_drone_count = 0;
  std::vector<DroneCenterListItem> unknown_items;
  std::vector<DroneCenterListItem> whitelist_items;
  std::vector<DroneCenterListItem> confirmed_items;
  std::vector<DroneCenterRadarPoint> radar_points;
};

void map_draw_control_panel(QPainter &p, int win_width, int win_height,
                            const MapControlPanelInput &in);

#endif