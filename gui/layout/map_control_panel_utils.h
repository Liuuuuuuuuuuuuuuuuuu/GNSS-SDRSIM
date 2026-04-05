#ifndef MAP_CONTROL_PANEL_UTILS_H
#define MAP_CONTROL_PANEL_UTILS_H

#include <QPainter>
#include <QString>

#include "gui/core/control_state.h"
#include "gui/core/signal_snapshot.h"

struct MapControlPanelInput {
  GuiControlState ctrl;
  TimeInfo time_info;
  QString rnx_name;
};

void map_draw_control_panel(QPainter &p, int win_width, int win_height,
                            const MapControlPanelInput &in);

#endif