#include "gui/map/map_osm_controls_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"

#include <QFontMetrics>
#include <QPen>

#include <algorithm>

namespace {

Rect qrect_to_rect(const QRect &q) {
  Rect r{q.x(), q.y(), q.width(), q.height()};
  return r;
}

} // namespace

void map_osm_draw_controls(QPainter &p, const MapOsmControlsInput &in,
                           MapOsmControlsState *out) {
  if (!out)
    return;

  const QColor btn_border("#b9cadf");
  const QColor btn_text("#f8fbff");
  const QColor btn_dim("#6b7b90");
  const QColor btn_stop("#ef5350");
  const QColor btn_nfz("#ef4444");
  const QColor btn_dark("#eab308");
  const QColor btn_tutorial("#7dd3fc");
  const QColor btn_back("#38bdf8");
  const QColor btn_return("#22c55e");

  out->back_btn_rect =
      QRect(in.panel.x() + in.panel.width() - 98, in.panel.y() + 44, 90, 26);
  if (in.running_ui) {
    Rect rr = qrect_to_rect(out->back_btn_rect);
    if (in.can_undo) {
      control_draw_button_filled(p, rr, btn_back, btn_back, QColor(8, 12, 18),
                                 "BACK");
    } else {
      control_draw_button(p, rr, btn_dim, btn_dim, "BACK");
    }
  }

  out->nfz_btn_rect =
      QRect(in.panel.x() + in.panel.width() - 192, in.panel.y() + 10, 90, 26);
  {
    Rect rr = qrect_to_rect(out->nfz_btn_rect);
    if (in.dji_on) {
      control_draw_button_filled(p, rr, btn_nfz, btn_nfz, QColor(8, 12, 18),
                                 "NFZ ON");
    } else {
      control_draw_button(p, rr, btn_border, btn_text, "NFZ OFF");
    }
  }

  out->dark_mode_btn_rect =
      QRect(in.panel.x() + in.panel.width() - 98, in.panel.y() + 10, 90, 26);
  {
    Rect rr = qrect_to_rect(out->dark_mode_btn_rect);
    if (in.dark_map_mode) {
      control_draw_button_filled(p, rr, btn_dark, btn_dark, QColor(8, 12, 18),
                                 "LIGHT");
    } else {
      control_draw_button(p, rr, btn_border, btn_text, "DARK");
    }
  }

  out->tutorial_toggle_rect = QRect();
  if (!in.running_ui) {
    out->tutorial_toggle_rect =
        QRect(in.panel.x() + in.panel.width() - 286, in.panel.y() + 10, 90, 26);
    Rect rr = qrect_to_rect(out->tutorial_toggle_rect);
    if (in.tutorial_enabled) {
      control_draw_button_filled(p, rr, btn_tutorial, btn_tutorial,
                                 QColor(8, 12, 18), "GUIDE ON");
    } else {
      control_draw_button(p, rr, btn_border, btn_text, "GUIDE OFF");
    }
  }

  out->search_return_btn_rect = QRect();
  if (in.show_search_return) {
    const QRect sb = in.search_box_rect.isEmpty()
                         ? QRect(in.panel.x() + 10, in.panel.y() + 10, 240, 30)
                         : in.search_box_rect;
    const int btn_w = 80;
    const int btn_h = sb.height();
    const int btn_x = sb.x();
    const int btn_y =
        std::min(sb.y() + sb.height() + 8, in.panel.y() + in.panel.height() - btn_h - 10);
    out->search_return_btn_rect = QRect(btn_x, btn_y, btn_w, btn_h);
    Rect rr = qrect_to_rect(out->search_return_btn_rect);
    control_draw_button_filled(p, rr, btn_return, btn_return, QColor(8, 12, 18),
                               "RETURN");
  }

  const bool show_tutorial_stop_preview =
      in.tutorial_overlay_visible && (in.tutorial_step == 13) && !in.running_ui;
  if (in.running_ui || show_tutorial_stop_preview) {
    QFontMetrics stop_fm(p.font());
    const char *kStopBtnLabel = "STOP";
    int stop_w = std::max(160, stop_fm.horizontalAdvance(kStopBtnLabel) + 48);
    stop_w = std::min(stop_w, in.panel.width() - 24);
    int stop_h = 46;
    int stop_y = in.panel.y() + in.panel.height() - stop_h - 72;
    out->osm_stop_btn_rect =
        QRect(in.panel.x() + (in.panel.width() - stop_w) / 2, stop_y, stop_w, stop_h);

    Rect rr = qrect_to_rect(out->osm_stop_btn_rect);
    if (in.running_ui) {
      control_draw_button_filled(p, rr, btn_stop, btn_stop, QColor(8, 12, 18),
                                 kStopBtnLabel);
    } else {
      control_draw_button(p, rr, btn_border, btn_text, kStopBtnLabel);
    }

    if (in.running_ui) {
      int hh = (int)(in.elapsed_sec / 3600);
      int mm = (int)((in.elapsed_sec % 3600) / 60);
      int ss = (int)(in.elapsed_sec % 60);

      QString run_txt =
          QString("%1 - %2:%3:%4")
              .arg(in.tx_active ? "RUN TIME" : "INIT TIME")
              .arg(hh, 2, 10, QChar('0'))
              .arg(mm, 2, 10, QChar('0'))
              .arg(ss, 2, 10, QChar('0'));

      QFont orig_font = p.font();
      QFont time_font = orig_font;
      time_font.setFamily("Monospace");
      time_font.setPointSize(24);
      time_font.setBold(true);
      time_font.setLetterSpacing(QFont::PercentageSpacing, 110);
      p.setFont(time_font);

      int txt_w = p.fontMetrics().horizontalAdvance(run_txt);
      int txt_h = p.fontMetrics().height();
      QRect time_rect(in.panel.x() + (in.panel.width() - txt_w) / 2 - 20,
                      stop_y - txt_h - 50, txt_w + 40, txt_h + 20);
      out->osm_runtime_rect = time_rect;

      p.setPen(QPen(QColor(255, 181, 71, 200), 2));
      p.setBrush(QColor(10, 20, 35, 220));
      p.drawRoundedRect(time_rect, 8, 8);
      p.setPen(QColor("#ffb547"));
      p.drawText(time_rect, Qt::AlignCenter, run_txt);

      p.setFont(orig_font);
      p.setRenderHint(QPainter::Antialiasing, false);
    } else {
      out->osm_runtime_rect = QRect();
    }
  } else {
    out->osm_stop_btn_rect = QRect();
    out->osm_runtime_rect = QRect();
  }

  if (in.dji_on) {
    int leg_w = 110, leg_h = 52;
    int leg_x = in.panel.x() + in.panel.width() - leg_w - 10;
    int leg_y = in.panel.y() + in.panel.height() - leg_h - 30;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(80, 100, 120, 150), 1));
    p.setBrush(QColor(10, 20, 35, 180));
    p.drawRect(leg_x, leg_y, leg_w, leg_h);

    QFont old_font = p.font();
    QFont leg_font = old_font;
    leg_font.setPointSize(old_font.pointSize() > 0 ? old_font.pointSize() - 1 : 9);
    p.setFont(leg_font);

    p.setPen(QPen(QColor(255, 0, 255, 240), 2));
    p.setBrush(QColor(255, 0, 255, 80));
    p.drawEllipse(leg_x + 10, leg_y + 14, 10, 10);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 28, leg_y + 24, "Restricted");

    p.setPen(QPen(QColor(59, 130, 246, 240), 2));
    p.setBrush(QColor(59, 130, 246, 60));
    p.drawEllipse(leg_x + 10, leg_y + 34, 10, 10);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 28, leg_y + 44, "Auth/Warn");

    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);
  }
}
