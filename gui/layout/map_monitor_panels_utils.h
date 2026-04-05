#ifndef MAP_MONITOR_PANELS_UTILS_H
#define MAP_MONITOR_PANELS_UTILS_H

#include <QImage>
#include <QPainter>

#include "gui/core/control_state.h"
#include "gui/core/signal_snapshot.h"

struct MapMonitorPanelsInput {
  GuiControlState ctrl;
  SpectrumSnapshot spec_snap;
  QImage waterfall_image;
};

void map_draw_spectrum_panel(QPainter &p, int win_width, int win_height,
                             const MapMonitorPanelsInput &in);
void map_draw_waterfall_panel(QPainter &p, int win_width, int win_height,
                              const MapMonitorPanelsInput &in);
void map_draw_time_panel(QPainter &p, int win_width, int win_height,
                         const MapMonitorPanelsInput &in);
void map_draw_constellation_panel(QPainter &p, int win_width, int win_height,
                                   const MapMonitorPanelsInput &in);
void map_update_waterfall_image(int win_width, int win_height,
                                const SpectrumSnapshot &snap, QImage *image,
                                int *image_width, int *image_height);

#endif