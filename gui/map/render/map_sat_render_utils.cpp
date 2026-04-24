#include "gui/map/render/map_sat_render_utils.h"

#include "gui/geo/geo_io.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/i18n/gui_font_manager.h"

#include <QColor>
#include <QPen>

#include <algorithm>
#include <cstdio>

extern "C" {
#include "globals.h"
#include "bdssim.h"
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

void draw_satellite_legend(QPainter &p, const QRect &map_rect,
                           const QColor &color_sat,
                           const QColor &color_sat_active,
                           const QColor &color_sat_selected_green,
                           const QColor &color_rx,
                           const QColor &color_rx_dim,
                           const QColor &color_text,
                           const QColor &color_text_dim,
                           const QColor &color_sat_outline,
                           const QColor &color_gps_sys,
                           const QColor &color_bds_sys,
                           GuiLanguage language,
                           bool receiver_valid,
                           bool has_visible,
                           bool has_standby,
                           bool has_running,
                           bool show_gps_row,
                           bool show_bds_row) {
  const QFont entry_font = p.font();
  p.setFont(gui_font_ui(language));
  QFont old_font = p.font();
  QFont legend_font = old_font;
  int pt = legend_font.pointSize();
  if (pt <= 0)
    pt = 10;
  legend_font.setPointSize(std::max(8, pt - 1));
  p.setFont(legend_font);

  QFontMetrics fm(legend_font);
  const QString lbl_receiver = gui_i18n_text(language, "map.sat_legend.receiver");
  const QString lbl_visible  = gui_i18n_text(language, "map.sat_legend.visible");
  const QString lbl_standby  = gui_i18n_text(language, "map.sat_legend.standby");
  const QString lbl_running  = gui_i18n_text(language, "map.sat_legend.running");
  const QString lbl_gps      = gui_i18n_text(language, "map.sat_legend.gps");
  const QString lbl_bds      = gui_i18n_text(language, "map.sat_legend.bds");

  const int dynamic_rows = (has_visible ? 1 : 0) + (has_standby ? 1 : 0) + (has_running ? 1 : 0);
  const int total_rows = 1 + dynamic_rows + (show_gps_row ? 1 : 0) +
                         (show_bds_row ? 1 : 0); // receiver + dynamic + SYS rows

  const int pad_x = 10;
  const int pad_y = 8;
  const int line_gap = 4;
  const int row_h = std::max(16, fm.height());
  const int icon_col_w = 24;
  int text_w = fm.horizontalAdvance(lbl_receiver);
  if (has_visible) text_w = std::max(text_w, fm.horizontalAdvance(lbl_visible));
  if (has_standby) text_w = std::max(text_w, fm.horizontalAdvance(lbl_standby));
  if (has_running)  text_w = std::max(text_w, fm.horizontalAdvance(lbl_running));
  if (show_gps_row) text_w = std::max(text_w, fm.horizontalAdvance(lbl_gps));
  if (show_bds_row) text_w = std::max(text_w, fm.horizontalAdvance(lbl_bds));

  const int legend_w = pad_x * 2 + icon_col_w + text_w;
  const int legend_h = pad_y * 2 + row_h * total_rows + line_gap * (total_rows - 1);
  const QRect legend_rect(map_rect.right() - legend_w - 10,
                          map_rect.bottom() - legend_h - 10,
                          legend_w, legend_h);

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor(120, 145, 172, 180), 1.0));
  p.setBrush(QColor(10, 20, 35, 196));
  p.drawRoundedRect(legend_rect, 6, 6);

  int row_idx = 0;
  auto row_y = [&](int i) {
    return legend_rect.y() + pad_y + i * (row_h + line_gap) + row_h / 2;
  };
  const int icon_x = legend_rect.x() + pad_x + 7;
  const int text_x = legend_rect.x() + pad_x + icon_col_w;

  // Receiver row
  {
    int ry = row_y(row_idx++);
    const QColor receiver_icon = receiver_valid ? color_rx : color_rx_dim;
    const QColor receiver_text = receiver_valid ? color_text : color_text_dim;
    p.setPen(QPen(receiver_icon, 2));
    p.drawLine(icon_x - 6, ry, icon_x + 6, ry);
    p.drawLine(icon_x, ry - 6, icon_x, ry + 6);
    p.setPen(Qt::NoPen);
    p.setBrush(receiver_icon);
    p.drawEllipse(QPoint(icon_x, ry), 2, 2);
    p.setPen(receiver_text);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_receiver);
  }

  // Visible row (yellow)
  if (has_visible) {
    int ry = row_y(row_idx++);
    p.setPen(Qt::NoPen);
    p.setBrush(color_sat_outline);
    p.drawEllipse(QPoint(icon_x, ry), 6, 6);
    p.setBrush(color_sat);
    p.drawEllipse(QPoint(icon_x, ry), 4, 4);
    p.setPen(color_text);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_visible);
  }

  // Standby row (green)
  if (has_standby) {
    int ry = row_y(row_idx++);
    p.setPen(Qt::NoPen);
    p.setBrush(color_sat_outline);
    p.drawEllipse(QPoint(icon_x, ry), 6, 6);
    p.setBrush(color_sat_selected_green);
    p.drawEllipse(QPoint(icon_x, ry), 4, 4);
    p.setPen(color_text);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_standby);
  }

  // Running row (red)
  if (has_running) {
    int ry = row_y(row_idx++);
    p.setPen(Qt::NoPen);
    p.setBrush(color_sat_outline);
    p.drawEllipse(QPoint(icon_x, ry), 6, 6);
    p.setBrush(color_sat_active);
    p.drawEllipse(QPoint(icon_x, ry), 4, 4);
    p.setPen(color_text);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_running);
  }

  // GPS row (system identifier)
  if (show_gps_row) {
    int ry = row_y(row_idx++);
    p.setPen(QPen(color_gps_sys, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(icon_x, ry), 6, 6);
    p.setPen(color_gps_sys);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_gps);
  }

  // BDS row (system identifier)
  if (show_bds_row) {
    int ry = row_y(row_idx++);
    p.setPen(QPen(color_bds_sys, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(icon_x, ry), 6, 6);
    p.setPen(color_bds_sys);
    p.drawText(QRect(text_x, ry - row_h / 2, text_w, row_h),
               Qt::AlignLeft | Qt::AlignVCenter, lbl_bds);
  }

  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(entry_font);
}

} // namespace

void map_draw_satellite_layer(QPainter &p, const QRect &map_rect,
                              const std::vector<SatPoint> &sats,
                              const GuiControlState &ctrl,
                              const int *active_prn_mask,
                              int active_prn_mask_len,
                              bool receiver_valid,
                              double receiver_lat_deg,
                              double receiver_lon_deg,
                              GuiLanguage language) {
  const QColor color_sat("#ffd166");
  const QColor color_sat_dim("#5b6472");
  const QColor color_sat_active("#ff4d6d");
  const QColor color_sat_selected_green("#22c55e");
  const QColor color_sat_outline("#111111");
  const QColor color_sat_outline_dim("#2f3744");
    const QColor color_gps_sys("#aaff00");
    const QColor color_bds_sys("#ff6600");
  const QColor color_rx("#00e5ff");
  const QColor color_rx_dim("#4b657d");
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

    // Set independent language font for all text in this layer
    p.setFont(gui_font_ui(language));

    // Bump label font slightly larger than base
    QFont sat_label_font = p.font();
    if (sat_label_font.pointSize() > 0)
      sat_label_font.setPointSize(std::max(9, sat_label_font.pointSize() + 1));

  for (const auto &sat : sats) {
    int x = map_rect.x() + lon_to_x(sat.lon_deg, map_rect.width());
    int y = map_rect.y() + lat_to_y(sat.lat_deg, map_rect.height());

    bool label_on = sat_label_enabled(sat, ctrl, candidate_mask);
    bool is_selected = (sat.prn >= 1 && sat.prn < active_prn_mask_len &&
                        active_prn_mask && active_prn_mask[sat.prn] != 0);
    QColor outline = label_on ? (sat.is_gps ? color_gps_sys : color_bds_sys)
                  : color_sat_outline_dim;
    QColor fill = label_on ? color_sat : color_sat_dim;

    p.setPen(Qt::NoPen);
    p.setBrush(outline);
      p.drawEllipse(QPoint(x, y), 8, 8);

    if (is_selected) {
      p.setBrush(ctrl.running_ui ? color_sat_active : color_sat_selected_green);
    } else {
      p.setBrush(fill);
    }
      p.drawEllipse(QPoint(x, y), 5, 5);

    char label[16];
    std::snprintf(label, sizeof(label), "%c%02d", sat.is_gps ? 'G' : 'C',
                  sat.prn);
    p.setPen(label_on ? (sat.is_gps ? color_gps_sys : color_bds_sys)
              : color_text_dim);
          p.setFont(sat_label_font);
          QRect label_rect(x + 10, y - 20, 52, 20);
          p.drawText(label_rect, Qt::AlignLeft | Qt::AlignVCenter,
                 QFontMetrics(sat_label_font).elidedText(label, Qt::ElideRight,
                                     label_rect.width()));
  }

  const bool mode_jam = (ctrl.interference_selection == 1);
  if (receiver_valid && !mode_jam) {
    int rx_x = map_rect.x() + lon_to_x(receiver_lon_deg, map_rect.width());
    int rx_y = map_rect.y() + lat_to_y(receiver_lat_deg, map_rect.height());
    p.setPen(QPen(color_rx, 2));
     p.drawLine(rx_x - 10, rx_y, rx_x + 10, rx_y);
     p.drawLine(rx_x, rx_y - 10, rx_x, rx_y + 10);
    p.setBrush(color_rx);
     p.drawEllipse(QPoint(rx_x, rx_y), 4, 4);
  }

  bool has_visible = false, has_selected = false;
  for (const auto &sat : sats) {
    bool label_on = sat_label_enabled(sat, ctrl, candidate_mask);
    bool is_sel = (sat.prn >= 1 && sat.prn < active_prn_mask_len &&
                   active_prn_mask && active_prn_mask[sat.prn] != 0);
    if (label_on && !is_sel) has_visible = true;
    if (is_sel) has_selected = true;
  }
  bool has_standby = has_selected && !ctrl.running_ui;
  bool has_running  = has_selected && ctrl.running_ui;
  const bool show_gps_row =
      (ctrl.signal_mode == SIG_MODE_GPS || ctrl.signal_mode == SIG_MODE_MIXED);
  const bool show_bds_row =
      (ctrl.signal_mode == SIG_MODE_BDS || ctrl.signal_mode == SIG_MODE_MIXED);

  draw_satellite_legend(p, map_rect, color_sat, color_sat_active,
                        color_sat_selected_green, color_rx, color_rx_dim,
                        color_text, color_text_dim, color_sat_outline,
                        color_gps_sys, color_bds_sys,
                        language, receiver_valid && !mode_jam,
                        has_visible, has_standby, has_running,
                        show_gps_row, show_bds_row);

  // Active satellite list panel (top-left of map)
  {
    std::vector<int> active_bds, active_gps;
    for (const auto &sat : sats) {
      bool is_active = (sat.prn >= 1 && sat.prn < active_prn_mask_len &&
                        active_prn_mask && active_prn_mask[sat.prn] != 0);
      if (!is_active) continue;
      if (sat.is_gps)
        active_gps.push_back(sat.prn);
      else
        active_bds.push_back(sat.prn);
    }

    // Only draw panel if there are active sats
    if (!active_bds.empty() || !active_gps.empty()) {
      p.setFont(gui_font_ui(language));
      QFont list_font = p.font();
      if (list_font.pointSize() > 0)
        list_font.setPointSize(std::max(8, list_font.pointSize() - 1));
      list_font.setBold(false);
      p.setFont(list_font);
      QFontMetrics lfm(list_font);

      // Build display strings
      QString bds_line, gps_line;
      if (!active_bds.empty()) {
        bds_line = "BDS: ";
        for (int prn : active_bds) {
          char buf[8]; std::snprintf(buf, sizeof(buf), "C%02d ", prn);
          bds_line += buf;
        }
        bds_line = bds_line.trimmed();
      }
      if (!active_gps.empty()) {
        gps_line = "GPS: ";
        for (int prn : active_gps) {
          char buf[8]; std::snprintf(buf, sizeof(buf), "G%02d ", prn);
          gps_line += buf;
        }
        gps_line = gps_line.trimmed();
      }

      const int pad_x = 10, pad_y = 8, line_gap = 4;
      const int row_h = std::max(16, lfm.height());
      int rows = (!bds_line.isEmpty() ? 1 : 0) + (!gps_line.isEmpty() ? 1 : 0);
      int text_w = 0;
      if (!bds_line.isEmpty()) text_w = std::max(text_w, lfm.horizontalAdvance(bds_line));
      if (!gps_line.isEmpty()) text_w = std::max(text_w, lfm.horizontalAdvance(gps_line));
      const int panel_w = pad_x * 2 + text_w;
      const int panel_h = pad_y * 2 + row_h * rows + line_gap * (rows - 1);
      const QRect panel_rect(map_rect.x() + 10, map_rect.y() + 10, panel_w, panel_h);

      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(QColor(120, 145, 172, 180), 1.0));
      p.setBrush(QColor(10, 20, 35, 196));
      p.drawRoundedRect(panel_rect, 6, 6);

      int row_idx = 0;
      auto draw_row = [&](const QString &text, const QColor &col) {
        int ry = panel_rect.y() + pad_y + row_idx * (row_h + line_gap);
        p.setPen(col);
        p.drawText(QRect(panel_rect.x() + pad_x, ry, text_w, row_h),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
        ++row_idx;
      };
      if (!bds_line.isEmpty()) draw_row(bds_line, color_bds_sys);
      if (!gps_line.isEmpty()) draw_row(gps_line, color_gps_sys);
    }
  }

  p.restore();
}