#include "gui/map/map_osm_controls_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"
#include "gui/core/gui_i18n.h"
#include "gui/core/gui_font_manager.h"

#include <QPainterPath>

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

  out->lang_btn_rect = QRect();

  const QColor btn_border("#b9cadf");
  const QColor btn_text("#f8fbff");
  const QColor btn_dim("#6b7b90");
  const QColor btn_stop("#ef5350");
  const QColor btn_nfz("#ef4444");
  const QColor btn_dark("#eab308");
  const QColor btn_tutorial("#7dd3fc");
  const QColor btn_back("#38bdf8");
  const QColor btn_return("#22c55e");

  const int btn_w = 90;
  const int btn_h = 26;
  const int col_gap = 8;
  const int row_gap = 8;
  const int col_right_x = in.panel.x() + in.panel.width() - btn_w - 8;
  const int col_left_x = col_right_x - col_gap - btn_w;
  const int row0_y = in.panel.y() + 10;
  const int row1_y = row0_y + btn_h + row_gap;
  const int row2_y = row1_y + btn_h + row_gap;

  out->lang_btn_rect = QRect(col_right_x, row0_y, btn_w, btn_h);
  {
    Rect rr = qrect_to_rect(out->lang_btn_rect);
    control_draw_button(p, rr, btn_border, btn_text,
                        gui_i18n_text(in.language, "osm.lang_btn").toUtf8().constData());
  }

  out->back_btn_rect = QRect(col_left_x, row1_y, btn_w, btn_h);
  if (in.running_ui) {
    Rect rr = qrect_to_rect(out->back_btn_rect);
    if (in.can_undo) {
      control_draw_button_filled(p, rr, btn_back, btn_back, QColor(8, 12, 18),
                                 gui_i18n_text(in.language, "osm.back").toUtf8().constData());
    } else {
      control_draw_button(p, rr, btn_dim, btn_dim,
                          gui_i18n_text(in.language, "osm.back").toUtf8().constData());
    }
  }

  out->nfz_btn_rect = QRect(col_left_x, row0_y, btn_w, btn_h);
  {
    Rect rr = qrect_to_rect(out->nfz_btn_rect);
    if (in.dji_on) {
      control_draw_button_filled(p, rr, btn_nfz, btn_nfz, QColor(8, 12, 18),
                                 gui_i18n_text(in.language, "osm.nfz_on").toUtf8().constData());
    } else {
      control_draw_button(p, rr, btn_border, btn_text,
                          gui_i18n_text(in.language, "osm.nfz_off").toUtf8().constData());
    }
  }

  out->dark_mode_btn_rect = QRect(col_right_x, row1_y, btn_w, btn_h);
  {
    Rect rr = qrect_to_rect(out->dark_mode_btn_rect);
    if (in.dark_map_mode) {
      control_draw_button_filled(p, rr, btn_dark, btn_dark, QColor(8, 12, 18),
                                 gui_i18n_text(in.language, "osm.light").toUtf8().constData());
    } else {
      control_draw_button(p, rr, btn_border, btn_text,
                          gui_i18n_text(in.language, "osm.dark").toUtf8().constData());
    }
  }

  out->tutorial_toggle_rect = QRect();
  if (!in.running_ui) {
    const int guide_w = 30;
    const int guide_x = col_right_x + btn_w - guide_w - 4;  // align to right edge with small margin
    const QRect guide_r(guide_x, row2_y, guide_w, btn_h);
    out->tutorial_toggle_rect = guide_r;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── Button base (rounded rect) ──────────────────────────────────────────
    const QColor bg_fill  = in.tutorial_enabled ? QColor(255, 228, 84, 245)
                                                : QColor(20, 28, 40, 200);
    const QColor bg_border = in.tutorial_enabled ? QColor(255, 252, 170)
                                                 : QColor(232, 210, 90, 190);
    p.setPen(QPen(bg_border, 1.2));
    p.setBrush(bg_fill);
    p.drawRoundedRect(guide_r, 8, 8);

    // ── Lightbulb icon only (no label text) ─────────────────────────────────
    const double cx = guide_r.x() + guide_r.width() * 0.5;
    const double cy = guide_r.y() + guide_r.height() * 0.5;
    const double br = 8.0;   // bulb radius

    const QColor bulb_col = in.tutorial_enabled ? QColor(255, 252, 186)
                           : QColor(180, 200, 220, 120);
    const QColor base_col = in.tutorial_enabled ? QColor(238, 192, 40)
                           : QColor(120, 140, 160, 140);

    // Outer glow (ON state only)
    if (in.tutorial_enabled) {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 243, 120, 92));
      p.drawEllipse(QPointF(cx, cy - 1.5), br + 6, br + 6);
    }

    // Bulb circle
    p.setPen(QPen(base_col, 1.2));
    p.setBrush(bulb_col);
    p.drawEllipse(QPointF(cx, cy - 1.5), br, br);

    // Filament (two arcs simulating a V inside the bulb)
    if (in.tutorial_enabled) {
      p.setPen(QPen(QColor(255, 200, 60), 1.0));
    } else {
      p.setPen(QPen(QColor(100, 120, 140, 160), 0.8));
    }
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(cx - 2, cy - 3.5), QPointF(cx,     cy - 0.5));
    p.drawLine(QPointF(cx,     cy - 0.5), QPointF(cx + 2, cy - 3.5));

    // Base lines (below bulb)
    p.setPen(QPen(base_col, 1.2));
    const double base_y = cy - 1.5 + br;
    p.drawLine(QPointF(cx - 3, base_y + 1.5), QPointF(cx + 3, base_y + 1.5));
    p.drawLine(QPointF(cx - 2, base_y + 3.0), QPointF(cx + 2, base_y + 3.0));

    p.restore();
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
                               gui_i18n_text(in.language, "osm.return").toUtf8().constData());
  }

      // Tutorial steps are zero-based. Map lower-half chapter may appear on step 1 or 2
      // depending on current guide flow mode, so allow both for stable preview.
    const bool show_tutorial_stop_preview =
        (in.force_stop_preview ||
         (in.tutorial_overlay_visible &&
          (in.tutorial_step == 1 || in.tutorial_step == 2))) && !in.running_ui;
  if (in.running_ui || show_tutorial_stop_preview) {
    QFontMetrics stop_fm(p.font());
    QByteArray stop_label = gui_i18n_text(in.language, "osm.stop").toUtf8();
    const char *kStopBtnLabel = stop_label.constData();
    int stop_w = std::max(160, stop_fm.horizontalAdvance(kStopBtnLabel) + 48);
    stop_w = std::min(stop_w, in.panel.width() - 24);
    int stop_h = 46;
    int stop_y = in.panel.y() + in.panel.height() - stop_h - 72;
    out->osm_stop_btn_rect =
        QRect(in.panel.x() + (in.panel.width() - stop_w) / 2, stop_y, stop_w, stop_h);

    Rect rr = qrect_to_rect(out->osm_stop_btn_rect);
    control_draw_button_filled(p, rr, btn_stop, btn_stop, QColor(8, 12, 18),
                               kStopBtnLabel);

    if (in.running_ui || show_tutorial_stop_preview) {
      int hh = (int)(in.elapsed_sec / 3600);
      int mm = (int)((in.elapsed_sec % 3600) / 60);
      int ss = (int)(in.elapsed_sec % 60);

      QString run_txt =
          QString("%1 - %2:%3:%4")
            .arg(gui_i18n_text(in.language,
                     in.tx_active ? "osm.run_time" : "osm.init_time"))
              .arg(hh, 2, 10, QChar('0'))
              .arg(mm, 2, 10, QChar('0'))
              .arg(ss, 2, 10, QChar('0'));
      if (show_tutorial_stop_preview) {
        run_txt = QString("%1 - 00:00:00")
                      .arg(gui_i18n_text(in.language, "osm.run_time"));
      }

      QFont orig_font = p.font();
      QFont time_font = gui_font_mono(24);
      time_font.setBold(true);
      time_font.setLetterSpacing(QFont::PercentageSpacing, 110);
      p.setFont(time_font);

      const QString init_txt =
          QString("%1 - 00:00:00").arg(gui_i18n_text(in.language, "osm.init_time"));
      const QString run_txt_sample =
          QString("%1 - 00:00:00").arg(gui_i18n_text(in.language, "osm.run_time"));
      int txt_w = p.fontMetrics().horizontalAdvance(run_txt);
      txt_w = std::max(txt_w, p.fontMetrics().horizontalAdvance(init_txt));
      txt_w = std::max(txt_w, p.fontMetrics().horizontalAdvance(run_txt_sample));
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
    int leg_w = 210, leg_h = 136;
    int leg_x = in.panel.x() + in.panel.width() - leg_w - 10;
    int leg_y = in.panel.y() + in.panel.height() - leg_h - 30;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(120, 145, 172, 180), 1.2));
    p.setBrush(QColor(10, 20, 35, 196));
    p.drawRoundedRect(leg_x, leg_y, leg_w, leg_h, 6, 6);

    QFont old_font = p.font();
    QFont title_font = old_font;
    title_font.setBold(true);
    title_font.setPointSize(old_font.pointSize() > 0 ? old_font.pointSize() : 10);
    p.setFont(title_font);
    p.setPen(QColor("#dbeafe"));
    p.drawText(QRect(leg_x + 10, leg_y + 6, leg_w - 20, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               gui_i18n_text(in.language, "osm.nfz_on"));

    QFont leg_font = old_font;
    leg_font.setPointSize(old_font.pointSize() > 0 ? old_font.pointSize() : 10);
    p.setFont(leg_font);

    p.setPen(QPen(QColor(220, 38, 38, 235), 2));
    p.setBrush(QColor(220, 38, 38, 88));
    p.drawEllipse(leg_x + 14, leg_y + 34, 12, 12);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 34, leg_y + 45,
           gui_i18n_text(in.language, "osm.legend_restricted_core"));

    p.setPen(QPen(QColor(217, 119, 6, 230), 2));
    p.setBrush(QColor(245, 158, 11, 75));
    p.drawEllipse(leg_x + 14, leg_y + 57, 12, 12);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 34, leg_y + 68,
           gui_i18n_text(in.language, "osm.legend_warning"));

    p.setPen(QPen(QColor(37, 99, 235, 230), 2));
    p.setBrush(QColor(59, 130, 246, 60));
    p.drawEllipse(leg_x + 14, leg_y + 80, 12, 12);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 34, leg_y + 91,
           gui_i18n_text(in.language, "osm.legend_authorization"));

    p.setPen(QPen(QColor(241, 245, 249, 230), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(leg_x + 14, leg_y + 100, 12, 12, 3, 3);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(leg_x + 34, leg_y + 111,
           gui_i18n_text(in.language, "osm.legend_service_white"));

    if (out) {
      out->nfz_legend_row_rects.clear();
      const int row_x = leg_x + 10;
      const int row_w = leg_w - 20;
      out->nfz_legend_row_rects.push_back(QRect(row_x, leg_y + 30, row_w, 22));  // restricted
      out->nfz_legend_row_rects.push_back(QRect(row_x, leg_y + 53, row_w, 22));  // warning
      out->nfz_legend_row_rects.push_back(QRect(row_x, leg_y + 76, row_w, 22));  // auth_warn
      out->nfz_legend_row_rects.push_back(QRect(row_x, leg_y + 96, row_w, 22));  // service_white
    }

    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);
  }
}
