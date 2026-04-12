#ifndef MAP_SAT_RENDER_UTILS_H
#define MAP_SAT_RENDER_UTILS_H

#include <QPainter>
#include <QRect>

#include <vector>

#include "gui/core/state/control_state.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/state/signal_snapshot.h"

void map_draw_satellite_layer(QPainter &p, const QRect &map_rect,
                              const std::vector<SatPoint> &sats,
                              const GuiControlState &ctrl,
                              const int *active_prn_mask,
                              int active_prn_mask_len,
                              bool receiver_valid,
                              double receiver_lat_deg,
                              double receiver_lon_deg,
                              GuiLanguage language);

#endif