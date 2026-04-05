#include "gui/map/map_sat_render_utils.h"

#include "gui/geo/geo_io.h"

#include <QColor>
#include <QPen>

#include <algorithm>
#include <cstdio>

extern "C" {
#include "globals.h"
}

namespace {

bool sat_label_enabled(const SatPoint &sat, const GuiControlState &ctrl,
                       const bool *candidate_mask) {
  if (ctrl.sat_mode == 0) {
    return sat.prn >= 1 && sat.prn < MAX_SAT && candidate_mask[sat.prn];
  }
  if (ctrl.sat_mode == 1) {
    return sat.is_gps ? (sat.prn >= 1 && sat.prn <= 32)
                      : (sat.prn >= 1 && sat.prn <= 37);
  }
  return true;
}

} // namespace

void map_draw_satellite_layer(QPainter &p, const QRect &map_rect,
                              const std::vector<SatPoint> &sats,
                              const GuiControlState &ctrl,
                              const int *active_prn_mask,
                              int active_prn_mask_len,
                              bool receiver_valid,
                              double receiver_lat_deg,
                              double receiver_lon_deg) {
  const QColor color_sat("#ffd166");
  const QColor color_sat_dim("#5b6472");
  const QColor color_sat_active("#ff4d6d");
  const QColor color_sat_selected_green("#22c55e");
  const QColor color_sat_outline("#111111");
  const QColor color_sat_outline_dim("#2f3744");
  const QColor color_rx("#00e5ff");
  const QColor color_text("#f7fbff");
  const QColor color_text_dim("#9aa9bc");

  bool candidate_mask[MAX_SAT] = {};
  if (ctrl.sat_mode == 0) {
    const int limit = std::min(ctrl.single_candidate_count,
                               (int)(sizeof(ctrl.single_candidates) /
                                     sizeof(ctrl.single_candidates[0])));
    for (int i = 0; i < limit; ++i) {
      int prn = ctrl.single_candidates[i];
      if (prn >= 1 && prn < MAX_SAT) {
        candidate_mask[prn] = true;
      }
    }
  }

  p.save();
  p.setClipRect(map_rect.adjusted(2, 2, -2, -2));

  for (const auto &sat : sats) {
    int x = map_rect.x() + lon_to_x(sat.lon_deg, map_rect.width());
    int y = map_rect.y() + lat_to_y(sat.lat_deg, map_rect.height());

    bool label_on = sat_label_enabled(sat, ctrl, candidate_mask);
    bool is_selected = (sat.prn >= 1 && sat.prn < active_prn_mask_len &&
                        active_prn_mask && active_prn_mask[sat.prn] != 0);
    QColor outline = label_on ? color_sat_outline : color_sat_outline_dim;
    QColor fill = label_on ? color_sat : color_sat_dim;

    p.setPen(Qt::NoPen);
    p.setBrush(outline);
    p.drawEllipse(QPoint(x, y), 6, 6);

    if (is_selected) {
      p.setBrush(ctrl.running_ui ? color_sat_active : color_sat_selected_green);
    } else {
      p.setBrush(fill);
    }
    p.drawEllipse(QPoint(x, y), 4, 4);

    char label[16];
    std::snprintf(label, sizeof(label), "%c%02d", sat.is_gps ? 'G' : 'C',
                  sat.prn);
    p.setPen(label_on ? color_text : color_text_dim);
    QRect label_rect(x + 8, y - 16, 44, 16);
    p.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(label, Qt::ElideRight,
                                          label_rect.width()));
  }

  if (receiver_valid) {
    int rx_x = map_rect.x() + lon_to_x(receiver_lon_deg, map_rect.width());
    int rx_y = map_rect.y() + lat_to_y(receiver_lat_deg, map_rect.height());
    p.setPen(QPen(color_rx, 2));
    p.drawLine(rx_x - 8, rx_y, rx_x + 8, rx_y);
    p.drawLine(rx_x, rx_y - 8, rx_x, rx_y + 8);
    p.setBrush(color_rx);
    p.drawEllipse(QPoint(rx_x, rx_y), 3, 3);
    QRect rx_lbl(rx_x + 9, rx_y - 14, 32, 14);
    p.drawText(rx_lbl, Qt::AlignLeft | Qt::AlignVCenter, "RX");
  }

  p.restore();
}