#ifndef MAP_MONITOR_PANELS_UTILS_H
#define MAP_MONITOR_PANELS_UTILS_H

#include <QImage>
#include <QPainter>

#include "gui/core/state/control_state.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/state/signal_snapshot.h"
#include "gui/layout/panels/map_control_panel_utils.h"

struct MapMonitorPanelsInput {
  GuiControlState ctrl;
  SpectrumSnapshot spec_snap;
  QImage waterfall_image;
  GuiLanguage language = GuiLanguage::English;
    std::vector<DroneCenterRadarPoint> radar_points;
    bool show_drone_center = false;
};

// 原始绘制函数（使用固定大小）
void map_draw_spectrum_panel(QPainter &p, int win_width, int win_height,
                             const MapMonitorPanelsInput &in);
void map_draw_waterfall_panel(QPainter &p, int win_width, int win_height,
                              const MapMonitorPanelsInput &in);
void map_draw_time_panel(QPainter &p, int win_width, int win_height,
                         const MapMonitorPanelsInput &in);
void map_draw_constellation_panel(QPainter &p, int win_width, int win_height,
                                   const MapMonitorPanelsInput &in);

// 新的绘制函数（支持动态展开）
void map_draw_spectrum_panel_expanded(QPainter &p, int win_width, int win_height,
                                      const MapMonitorPanelsInput &in,
                                      double expand_progress);
void map_draw_waterfall_panel_expanded(QPainter &p, int win_width, int win_height,
                                       const MapMonitorPanelsInput &in,
                                       double expand_progress);
void map_draw_time_panel_expanded(QPainter &p, int win_width, int win_height,
                                  const MapMonitorPanelsInput &in,
                                  double expand_progress);
void map_draw_constellation_panel_expanded(QPainter &p, int win_width, int win_height,
                                            const MapMonitorPanelsInput &in,
                                            double expand_progress);

void map_update_waterfall_image(int win_width, int win_height,
                                const SpectrumSnapshot &snap, QImage *image,
                                int *image_width, int *image_height);

#endif