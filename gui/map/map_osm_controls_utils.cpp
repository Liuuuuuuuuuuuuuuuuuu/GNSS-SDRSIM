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

  p.setFont(gui_font_ui(in.language));

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
                                 gui_i18n_text(in.language, "osm.dark").toUtf8().constData());
    } else {
      control_draw_button(p, rr, btn_border, btn_text,
                          gui_i18n_text(in.language, "osm.light").toUtf8().constData());
    }
  }

  out->tutorial_toggle_rect = QRect();
  if (!in.running_ui) {
    const int guide_w = 60;
    const int guide_h = btn_h * 2;
    const int guide_x = col_right_x + btn_w - guide_w - 4;  // align to right edge with small margin
    const QRect guide_r(guide_x, row2_y, guide_w, guide_h);
    out->tutorial_toggle_rect = guide_r;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── Button base (rounded rect) ──────────────────────────────────────────
    const bool bulb_active = in.tutorial_enabled || in.tutorial_hovered;
    const QColor bg_fill = bulb_active ? QColor(255, 248, 165, 248)
           : QColor(86, 78, 40, 228);
    // Keep a clear yellow frame even in default (inactive) state.
    const QColor bg_border = bulb_active ? QColor(255, 255, 230)
           : QColor(255, 232, 96, 250);
    p.setPen(QPen(bg_border, bulb_active ? 1.2 : 1.8));
    p.setBrush(bg_fill);
    p.drawRoundedRect(guide_r, 8, 8);

    // ── Lightbulb icon only (no label text) ─────────────────────────────────
    const double cx = guide_r.x() + guide_r.width() * 0.5;
    const double cy = guide_r.y() + guide_r.height() * 0.5;
    const double br = 8.0;   // bulb radius

    const QColor bulb_col = bulb_active ? QColor(255, 255, 242)
              : QColor(255, 244, 185, 240);
    const QColor base_col = bulb_active ? QColor(255, 228, 80)
                      : QColor(255, 206, 74, 230);

    // Outer glow on enabled or hover, stronger when enabled.
    if (bulb_active) {
      p.setPen(Qt::NoPen);
      p.setBrush(in.tutorial_enabled ? QColor(255, 250, 150, 150)
                                     : QColor(255, 240, 130, 118));
      p.drawEllipse(QPointF(cx, cy - 1.5), br + (in.tutorial_enabled ? 8 : 7),
                    br + (in.tutorial_enabled ? 8 : 7));
      p.setBrush(in.tutorial_enabled ? QColor(255, 248, 170, 100)
                                     : QColor(255, 234, 130, 82));
      p.drawEllipse(QPointF(cx, cy - 1.5), br + (in.tutorial_enabled ? 12 : 10),
                    br + (in.tutorial_enabled ? 12 : 10));
      if (in.tutorial_hovered) {
        p.setBrush(QColor(255, 245, 170, 64));
        p.drawEllipse(QPointF(cx, cy - 1.5), br + 14, br + 14);
      }
    } else {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 224, 92, 74));
      p.drawEllipse(QPointF(cx, cy - 1.5), br + 6, br + 6);
    }

    // Bulb circle
    p.setPen(QPen(base_col, 1.2));
    p.setBrush(bulb_col);
    p.drawEllipse(QPointF(cx, cy - 1.5), br, br);

    // Filament (two arcs simulating a V inside the bulb)
    if (bulb_active) {
      p.setPen(QPen(QColor(255, 214, 72), 1.2));
    } else {
      p.setPen(QPen(QColor(255, 198, 88, 220), 1.0));
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
  if (in.running_ui) {
    QFontMetrics stop_fm(p.font());
    QByteArray stop_label = gui_i18n_text(in.language, "osm.stop").toUtf8();
    const char *kStopBtnLabel = stop_label.constData();
    int stop_w = std::max(132, stop_fm.horizontalAdvance(kStopBtnLabel) + 38);
    stop_w = std::min(stop_w, in.panel.width() - 24);
    int stop_h = std::max(38, std::min(46, in.panel.height() / 5));
    const int bottom_pad = std::max(40, std::min(72, in.panel.height() / 6));
    int stop_y = in.panel.y() + in.panel.height() - stop_h - bottom_pad;
    out->osm_stop_btn_rect =
        QRect(in.panel.x() + (in.panel.width() - stop_w) / 2, stop_y, stop_w, stop_h);

    Rect rr = qrect_to_rect(out->osm_stop_btn_rect);
    control_draw_button_filled(p, rr, btn_stop, btn_stop, QColor(8, 12, 18),
                               kStopBtnLabel);

    {
      int hh = (int)(in.elapsed_sec / 3600);
      int mm = (int)((in.elapsed_sec % 3600) / 60);
      int ss = (int)(in.elapsed_sec % 60);

      QString run_txt;
      if (in.tx_active) {
        run_txt = QString("%1 - %2:%3:%4")
                      .arg(gui_i18n_text(in.language, "osm.run_time"))
                      .arg(hh, 2, 10, QChar('0'))
                      .arg(mm, 2, 10, QChar('0'))
                      .arg(ss, 2, 10, QChar('0'));
      } else {
        run_txt = gui_i18n_text(in.language, "osm.init_time");
      }

      QFont orig_font = p.font();
      const int time_font_pt = std::max(18, std::min(24, in.panel.height() / 11));
      QFont time_font = gui_font_mono(time_font_pt);
      time_font.setLetterSpacing(QFont::PercentageSpacing, 106);
      p.setFont(time_font);

      const QString init_txt = gui_i18n_text(in.language, "osm.init_time");
      const QString run_txt_sample =
          QString("%1 - 00:00:00").arg(gui_i18n_text(in.language, "osm.run_time"));
      int txt_w = p.fontMetrics().horizontalAdvance(run_txt);
      txt_w = std::max(txt_w, p.fontMetrics().horizontalAdvance(init_txt));
      txt_w = std::max(txt_w, p.fontMetrics().horizontalAdvance(run_txt_sample));
      int txt_h = p.fontMetrics().height();
      const int time_hpad = std::max(12, std::min(18, in.panel.width() / 30));
      const int time_vpad = std::max(6, std::min(10, in.panel.height() / 45));
      const int runtime_gap = std::max(22, std::min(38, in.panel.height() / 9));
      QRect time_rect(in.panel.x() + (in.panel.width() - txt_w) / 2 - time_hpad,
              stop_y - txt_h - runtime_gap,
              txt_w + time_hpad * 2,
              txt_h + time_vpad * 2);
      out->osm_runtime_rect = time_rect;

      p.setPen(QPen(QColor(255, 181, 71, 200), 2));
      p.setBrush(QColor(10, 20, 35, 220));
      p.drawRoundedRect(time_rect, 8, 8);
      p.setPen(QColor("#ffb547"));
      p.drawText(time_rect, Qt::AlignCenter, run_txt);

      p.setFont(orig_font);
      p.setRenderHint(QPainter::Antialiasing, false);
    }
  } else {
    out->osm_stop_btn_rect = QRect();
    out->osm_runtime_rect = QRect();
  }

  if (in.dji_on) {
    QFont old_font = p.font();
    QFont title_font = old_font;
    title_font.setPointSize(old_font.pointSize() > 0 ? old_font.pointSize() : 10);
    QFont leg_font = old_font;
    leg_font.setPointSize(old_font.pointSize() > 0 ? old_font.pointSize() : 10);

    const QString row_restricted =
        gui_i18n_text(in.language, "osm.legend_restricted_core");

    const int min_leg_w = 156;
    const int max_leg_w = std::max(min_leg_w, in.panel.width() - 20);
    const int row_icon_x = 12;
    const int row_text_x = row_icon_x + 20;
    const int right_pad = 14;
    const int row_gap = 23;
    const int box_top = 12;
    const int bottom_pad = 12;
    const int icon_size = 12;

    int leg_w = min_leg_w;
    int leg_h = 40;
    QFontMetrics leg_fm(leg_font);

    auto content_width = [&](const QFontMetrics &row_metrics) {
      return row_text_x + row_metrics.horizontalAdvance(row_restricted) + right_pad;
    };

    leg_w = std::max(min_leg_w, content_width(leg_fm));
    if (leg_w > max_leg_w) {
      leg_font.setPointSize(std::max(8, leg_font.pointSize() - 1));
      leg_fm = QFontMetrics(leg_font);
      leg_w = std::max(min_leg_w, std::min(max_leg_w, content_width(leg_fm)));
    }

    leg_h = std::max(40, box_top + icon_size + bottom_pad);

    int leg_x = in.panel.x() + in.panel.width() - leg_w - 10;
    int leg_y = in.panel.y() + in.panel.height() - leg_h - 30;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(120, 145, 172, 180), 1.2));
    p.setBrush(QColor(10, 20, 35, 196));
    p.drawRoundedRect(leg_x, leg_y, leg_w, leg_h, 6, 6);

    p.setFont(leg_font);

    p.setPen(QPen(QColor(220, 38, 38, 235), 2));
    p.setBrush(QColor(220, 38, 38, 88));
    p.drawEllipse(leg_x + row_icon_x, leg_y + box_top, icon_size, icon_size);
    p.setPen(QColor("#f1f7ff"));
    p.drawText(QRect(leg_x + row_text_x, leg_y + box_top - 1,
                     leg_w - row_text_x - right_pad, row_gap),
               Qt::AlignLeft | Qt::AlignVCenter, row_restricted);

    if (out) {
      out->nfz_legend_row_rects.clear();
    }

    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);
  }
}
