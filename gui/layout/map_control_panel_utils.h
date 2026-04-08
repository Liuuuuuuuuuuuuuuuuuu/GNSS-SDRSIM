#ifndef MAP_CONTROL_PANEL_UTILS_H
#define MAP_CONTROL_PANEL_UTILS_H

#include <QPainter>
#include <QString>
#include <QColor>

#include "gui/core/control_state.h"
#include "gui/core/gui_i18n.h"
#include "gui/core/signal_snapshot.h"

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
};

void map_draw_control_panel(QPainter &p, int win_width, int win_height,
                            const MapControlPanelInput &in);

#endif