#include "gui/control/panel/control_paint.h"
#include "gui/control/panel/control_paint_internal.h"

#include <QPainterPath>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>

void control_draw_slider(QPainter &p, const Rect &r, const QColor &border,
                         const QColor &text, const QColor &dim,
                         const QColor &accent, const char *name,
                         const char *value, double ratio, bool enabled) {
  QFont old_font = p.font();
  QFont small_font = old_font;
  const QString name_text = QString::fromUtf8(name ? name : "");
  const QString value_text = QString::fromUtf8(value ? value : "");
  QColor edge = enabled ? border : dim;
  QColor t = enabled ? text : dim;
  QColor f_color = enabled ? accent : QColor(71, 85, 105, 150);
  ratio = control_paint_clamp_double(ratio, 0.0, 1.0);

  Rect vrect = slider_value_rect(r);
  int label_w =
      control_paint_clamp_int((int)std::lround((double)r.w * 0.34), 86, 190);
  int label_x = r.x;
  int min_track_x = label_x + label_w + 8;
  if (vrect.x <= min_track_x + 20) {
    label_w = std::max(52, vrect.x - r.x - 28);
    min_track_x = label_x + label_w + 8;
  }

  QRect label_rect(label_x, r.y, std::max(24, label_w), r.h);
  control_paint_apply_slider_label_adjustment(&label_rect);
  if (label_rect.width() < 24)
    label_rect.setWidth(24);
  int track_start = std::min(std::max(min_track_x, r.x + 8), vrect.x - 20);
  int track_end = vrect.x - 10;
  int track_w = std::max(10, track_end - track_start);

  QRect value_rect(vrect.x, vrect.y, vrect.w, vrect.h);
  control_paint_apply_slider_value_adjustment(&value_rect);
  vrect.x = value_rect.x();
  vrect.y = value_rect.y();
  vrect.w = std::max(10, value_rect.width());
  vrect.h = std::max(10, value_rect.height());

  QRect track_rect(track_start, r.y + r.h / 2 - ((r.h > 24) ? 8 : 6) / 2,
                   track_w, (r.h > 24) ? 8 : 6);
  control_paint_apply_slider_track_adjustment(&track_rect);
  track_start = track_rect.x();
  track_w = std::max(10, track_rect.width());

  int label_fit_pt = control_paint_fit_text_point_size(
      old_font, name_text,
      std::max(12, label_rect.width() - 2), std::max(8, label_rect.height() - 2),
      11, control_paint_clamp_int(std::max(12, (int)std::round(r.h * 0.66)), 12,
                                  24));
  if (control_paint_uniform_label_pt() > 0)
    label_fit_pt = control_paint_uniform_label_pt();
  if (control_paint_uniform_text_pt() > 0)
    label_fit_pt = std::max(label_fit_pt, control_paint_uniform_text_pt());
  small_font.setPointSize(label_fit_pt);
  p.setFont(small_font);
  p.setPen(t);
  p.drawText(label_rect, Qt::AlignVCenter | Qt::AlignLeft,
             p.fontMetrics().elidedText(name_text, Qt::ElideRight, label_rect.width()));

  QPainterPath vpath;
  int cl = 4;
  vpath.moveTo(vrect.x + cl, vrect.y);
  vpath.lineTo(vrect.x + vrect.w, vrect.y);
  vpath.lineTo(vrect.x + vrect.w, vrect.y + vrect.h - cl);
  vpath.lineTo(vrect.x + vrect.w - cl, vrect.y + vrect.h);
  vpath.lineTo(vrect.x, vrect.y + vrect.h);
  vpath.lineTo(vrect.x, vrect.y + cl);
  vpath.closeSubpath();

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(edge, 1));
  p.setBrush(QColor(10, 20, 35, 220));
  p.drawPath(vpath);
  p.setPen(enabled ? QColor("#00ffcc") : dim);

  QFont fb = old_font;
  int value_pt = control_paint_fit_text_point_size(
      old_font, value_text, std::max(12, vrect.w - 10),
      std::max(10, vrect.h - 6), 9,
      control_paint_clamp_int(std::max(10, (int)std::round(vrect.h * 0.52)), 10,
                              16),
      true, control_paint_value_text_scale());
  if (control_paint_uniform_text_pt() > 0)
    value_pt = std::max(value_pt, control_paint_uniform_text_pt());
  fb.setPointSize(value_pt);
  p.setFont(fb);
  QRect value_text_rect(vrect.x + 5, vrect.y + 2, std::max(4, vrect.w - 10),
                        std::max(4, vrect.h - 4));
  p.drawText(value_text_rect, Qt::AlignCenter, value_text);
  p.setFont(old_font);

  int track_thick = std::max(4, track_rect.height());
  int ty = track_rect.y() + track_rect.height() / 2;
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(30, 45, 60, 180));
  p.drawRect(track_start, ty - track_thick / 2, track_w, track_thick);

  int fill_w = (int)llround((double)track_w * ratio);
  if (fill_w > 0) {
    p.setBrush(f_color);
    p.drawRect(track_start, ty - track_thick / 2, fill_w, track_thick);
  }

  if (std::strcmp(name, "FS (Frequency)") == 0) {
    QFont orig_font = p.font();
    auto draw_tick = [&](double val, const char *lbl) {
      double tick_ratio = (val - 2.6) / std::max(0.1, 31.2 - 2.6);
      int tx = track_start + (int)llround(track_w * tick_ratio);

      p.setPen(QPen(QColor(139, 195, 255, 200), 2));
      p.drawLine(tx, ty - track_thick / 2 - 3, tx, ty + track_thick / 2 + 3);

      QFont f = orig_font;
      f.setPointSize(f.pointSize() - 1);
      p.setFont(f);
      p.setPen(QColor(139, 195, 255, 255));
      int tick_bot = ty + track_thick / 2 + 3;
      p.drawText(QRect(tx - 14, tick_bot + 5, 28, 18),
                 Qt::AlignTop | Qt::AlignHCenter, QString(lbl));
    };
    p.setRenderHint(QPainter::Antialiasing, false);
    draw_tick(2.6, "2.6");
    draw_tick(5.2, "5.2");
    draw_tick(20.8, "20.8");
    p.setFont(orig_font);
  }

  int knob_size = (r.h > 24) ? 10 : 7;
  int knob_x = track_start + fill_w;
  QPainterPath knob;
  knob.moveTo(knob_x, ty - knob_size);
  knob.lineTo(knob_x + knob_size, ty);
  knob.lineTo(knob_x, ty + knob_size);
  knob.lineTo(knob_x - knob_size, ty);
  knob.closeSubpath();

  p.setPen(QPen(edge, 1));
  p.setBrush(enabled ? QColor("#ffffff") : dim);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.drawPath(knob);
  p.setRenderHint(QPainter::Antialiasing, false);
  if (!enabled)
    control_paint_paint_disabled_overlay(p, r, 6);
  p.setFont(old_font);
}

void control_draw_slider_stacked(QPainter &p, const Rect &r,
                                 const QColor &border, const QColor &text,
                                 const QColor &dim, const QColor &accent,
                                 const char *name, const char *value,
                                 double ratio, bool enabled,
                                 bool show_fs_ticks,
                                 bool emphasize_caption) {
  QFont old_font = p.font();
  const QString name_text = QString::fromUtf8(name ? name : "");
  const QString value_text = QString::fromUtf8(value ? value : "");
  QColor edge = enabled ? border : dim;
  QColor t = enabled ? text : dim;
  QColor f_color = enabled ? accent : QColor(71, 85, 105, 150);
  ratio = control_paint_clamp_double(ratio, 0.0, 1.0);

  int caption_h =
      control_paint_clamp_int((int)std::lround((double)r.h * 0.36), 14, 24);
  Rect lower = {r.x, r.y + caption_h, r.w, std::max(12, r.h - caption_h)};
  Rect vrect = slider_value_rect(lower);

  QFont caption_font = old_font;
  int caption_pt = control_paint_fit_text_point_size(
      old_font, name_text, std::max(12, r.w - 8),
      std::max(10, caption_h - 2), 10, emphasize_caption ? 18 : 16, false,
      emphasize_caption ? 1.04 : 1.0);
  if (control_paint_uniform_text_pt() > 0)
    caption_pt = std::max(caption_pt, control_paint_uniform_text_pt());
  caption_font.setPointSize(caption_pt);
  p.setFont(caption_font);
  p.setPen(t);
  QRect caption_rect(r.x + 2, r.y + 1, std::max(8, r.w - 4),
                     std::max(8, caption_h - 2));
  control_paint_apply_slider_label_adjustment(&caption_rect);
  p.drawText(caption_rect, Qt::AlignVCenter | Qt::AlignLeft,
             p.fontMetrics().elidedText(name_text,
                                        Qt::ElideRight, caption_rect.width()));

  int track_start = lower.x + 8;
  int track_end = vrect.x - 10;
  int track_w = std::max(10, track_end - track_start);

  QRect value_rect(vrect.x, vrect.y, vrect.w, vrect.h);
  control_paint_apply_slider_value_adjustment(&value_rect);
  vrect.x = value_rect.x();
  vrect.y = value_rect.y();
  vrect.w = std::max(10, value_rect.width());
  vrect.h = std::max(10, value_rect.height());

  QRect track_rect(track_start,
                   lower.y + lower.h / 2 - ((lower.h > 24) ? 8 : 6) / 2,
                   track_w, (lower.h > 24) ? 8 : 6);
  control_paint_apply_slider_track_adjustment(&track_rect);
  track_start = track_rect.x();
  track_w = std::max(10, track_rect.width());
  int track_thick = std::max(4, track_rect.height());
  int ty = track_rect.y() + track_rect.height() / 2;

  QPainterPath vpath;
  int cl = 4;
  vpath.moveTo(vrect.x + cl, vrect.y);
  vpath.lineTo(vrect.x + vrect.w, vrect.y);
  vpath.lineTo(vrect.x + vrect.w, vrect.y + vrect.h - cl);
  vpath.lineTo(vrect.x + vrect.w - cl, vrect.y + vrect.h);
  vpath.lineTo(vrect.x, vrect.y + vrect.h);
  vpath.lineTo(vrect.x, vrect.y + cl);
  vpath.closeSubpath();

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(edge, 1));
  p.setBrush(QColor(10, 20, 35, 220));
  p.drawPath(vpath);
  p.setPen(enabled ? QColor("#00ffcc") : dim);

  QFont value_font = old_font;
  int value_pt = control_paint_fit_text_point_size(
      old_font, value_text, std::max(12, vrect.w - 10),
      std::max(10, vrect.h - 6), 9, 16, true,
      control_paint_value_text_scale());
  if (control_paint_uniform_text_pt() > 0)
    value_pt = std::max(value_pt, control_paint_uniform_text_pt());
  value_font.setPointSize(value_pt);
  p.setFont(value_font);
  p.drawText(QRect(vrect.x + 5, vrect.y + 2, std::max(4, vrect.w - 10),
                   std::max(4, vrect.h - 4)),
       Qt::AlignCenter, value_text);

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(30, 45, 60, 180));
  p.drawRect(track_start, ty - track_thick / 2, track_w, track_thick);

  int fill_w = (int)llround((double)track_w * ratio);
  if (fill_w > 0) {
    p.setBrush(f_color);
    p.drawRect(track_start, ty - track_thick / 2, fill_w, track_thick);
  }

  if (show_fs_ticks) {
    QFont tick_font = old_font;
    tick_font.setPointSize(control_paint_clamp_int(old_font.pointSize() - 1, 8, 12));
    p.setFont(tick_font);
    auto draw_tick = [&](double val, const char *lbl) {
      double tick_ratio = (val - 2.6) / std::max(0.1, 31.2 - 2.6);
      int tx = track_start + (int)llround(track_w * tick_ratio);
      p.setPen(QPen(QColor(139, 195, 255, 210), 2));
      p.drawLine(tx, ty - track_thick / 2 - 3, tx, ty + track_thick / 2 + 3);
      p.setPen(QColor(139, 195, 255, 255));
      int tick_bot = ty + track_thick / 2 + 3;
      int label_top = tick_bot + 5;
      int label_bot = std::min(lower.y + lower.h - 1, label_top + 18);
      p.drawText(QRect(tx - 14, label_top, 28, label_bot - label_top),
                 Qt::AlignTop | Qt::AlignHCenter, QString::fromUtf8(lbl));
    };
    p.setRenderHint(QPainter::Antialiasing, false);
    draw_tick(2.6, "2.6");
    draw_tick(5.2, "5.2");
    draw_tick(20.8, "20.8");
  }

  int knob_size = (lower.h > 24) ? 10 : 7;
  int knob_x = track_start + fill_w;
  QPainterPath knob;
  knob.moveTo(knob_x, ty - knob_size);
  knob.lineTo(knob_x + knob_size, ty);
  knob.lineTo(knob_x, ty + knob_size);
  knob.lineTo(knob_x - knob_size, ty);
  knob.closeSubpath();

  p.setPen(QPen(edge, 1));
  p.setBrush(enabled ? QColor("#ffffff") : dim);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.drawPath(knob);
  p.setRenderHint(QPainter::Antialiasing, false);
  if (!enabled)
    control_paint_paint_disabled_overlay(p, r, 6);
  p.setFont(old_font);
}

void control_draw_three_switch(QPainter &p, const Rect &r, const QColor &border,
                               const QColor &text, const QColor &dim,
                               const QColor &active_fill, const char *caption,
                               const char *a, const char *b, const char *c,
                               int active_idx, bool enabled) {
  const QString label_texts[3] = {
      QString::fromUtf8(a ? a : ""),
      QString::fromUtf8(b ? b : ""),
      QString::fromUtf8(c ? c : "")};
  QColor edge = enabled ? border : dim;
  QColor t = enabled ? text : dim;
  QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);

  QFont old_font = p.font();
  QFont fcap = old_font;
  QString caption_text = QString::fromUtf8(caption ? caption : "");
  int caption_h =
      control_paint_clamp_int((int)std::lround((double)r.h * 0.36), 14, 24);
  int min_seg_h =
      control_paint_clamp_int((int)std::lround((double)r.h * 0.48), 14, 26);
  int max_caption_h = std::max(12, r.h - min_seg_h);
  max_caption_h = std::min(max_caption_h, caption_h + 2);
  int caption_pt = control_paint_fit_text_point_size(
      old_font, caption_text, std::max(12, r.w - 12),
      std::max(10, max_caption_h - 2), 10, 18, false,
      control_paint_caption_text_scale());
  if (control_paint_uniform_text_pt() > 0)
    caption_pt = std::max(caption_pt, control_paint_uniform_text_pt());
  fcap.setPointSize(caption_pt);
  p.setFont(fcap);
  caption_h = std::max(caption_h, QFontMetrics(fcap).height() + 4);
  caption_h = control_paint_clamp_int(caption_h, 12, std::max(12, r.h - min_seg_h));

  QFont fopt_probe = old_font;
  int option_pt_probe = control_paint_scale_pt_with_factor(
      std::max(8, caption_pt - 1), control_paint_switch_option_text_scale());
  option_pt_probe = control_paint_clamp_int(option_pt_probe, 8, 16);
  fopt_probe.setPointSize(option_pt_probe);
  QFontMetrics fm_opt_probe(fopt_probe);
  int wanted_seg_h = control_paint_clamp_int(
      fm_opt_probe.height() + 12, 16, std::max(16, r.h - caption_h));
  int total_wanted_h = caption_h + wanted_seg_h;
  int top_pad = (r.h > total_wanted_h) ? ((r.h - total_wanted_h) / 2) : 0;
  int base_y = r.y + top_pad;

  const int seg_y = base_y + caption_h;
  const int seg_h =
      std::max(min_seg_h, std::min(wanted_seg_h, r.y + r.h - seg_y));
  const int seg_draw_h = std::max(12, (int)std::lround((double)seg_h * 0.8));
  const int seg_draw_y = seg_y + std::max(0, (seg_h - seg_draw_h) / 2);

  p.setPen(t);
  p.drawText(QRect(r.x, base_y + 2, std::max(12, r.w), std::max(10, caption_h - 4)),
             Qt::AlignVCenter | Qt::AlignLeft,
             p.fontMetrics().elidedText(caption_text, Qt::ElideRight,
                                        std::max(12, r.w)));

  QPainterPath path;
  int cl = 8;
  path.moveTo(r.x + cl, seg_draw_y);
  path.lineTo(r.x + r.w - cl, seg_draw_y);
  path.lineTo(r.x + r.w, seg_draw_y + cl);
  path.lineTo(r.x + r.w, seg_draw_y + seg_draw_h - cl);
  path.lineTo(r.x + r.w - cl, seg_draw_y + seg_draw_h);
  path.lineTo(r.x + cl, seg_draw_y + seg_draw_h);
  path.lineTo(r.x, seg_draw_y + seg_draw_h - cl);
  path.lineTo(r.x, seg_draw_y + cl);
  path.closeSubpath();

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(edge, 1));
  p.setBrush(QColor(10, 20, 30, 200));
  p.drawPath(path);

  int seg_w = r.w / 3;
  for (int i = 0; i < 3; ++i) {
    int sx = r.x + i * seg_w;
    int sw = (i == 2) ? (r.w - 2 * seg_w) : seg_w;
    QRect seg(sx, seg_draw_y, sw, seg_draw_h);

    if (i == active_idx) {
      p.setPen(Qt::NoPen);
      p.setBrush(active);
      p.drawRect(sx + 4, seg_draw_y + seg_draw_h - 6, sw - 8, 4);
      p.setBrush(QColor(active.red(), active.green(), active.blue(), 60));
      p.drawRect(sx + 1, seg_draw_y + 1, sw - 2, seg_draw_h - 2);
    }
    if (i > 0) {
      p.setPen(QPen(QColor(edge.red(), edge.green(), edge.blue(), 100), 1));
      p.drawLine(sx, seg_draw_y + 6, sx, seg_draw_y + seg_draw_h - 6);
    }
    p.setPen(i == active_idx ? QColor("#ffffff") : t);
    QFont f = p.font();
    f.setBold(i == active_idx);
    p.setFont(f);
    QRect seg_text_rect = seg.adjusted(10, 3, -10, -3);
    int option_pt = control_paint_fit_text_point_size(
      old_font, label_texts[i],
        std::max(8, seg_text_rect.width()), std::max(8, seg_text_rect.height()), 8,
        control_paint_clamp_int(
            std::max(9, (int)std::lround((double)seg_draw_h * 0.46)), 9, 18),
        i == active_idx, control_paint_switch_option_text_scale());
    if (control_paint_uniform_text_pt() > 0)
      option_pt = std::max(option_pt, control_paint_uniform_text_pt());
    f.setPointSize(option_pt);
    p.setFont(f);
    p.drawText(seg_text_rect, Qt::AlignCenter,
               p.fontMetrics().elidedText(label_texts[i], Qt::ElideRight,
                                          std::max(8, seg_text_rect.width())));
    f.setBold(false);
    p.setFont(f);
  }
  p.setRenderHint(QPainter::Antialiasing, false);
  if (!enabled)
    control_paint_paint_disabled_overlay(p, r, 8);
  p.setFont(old_font);
}

void control_draw_two_switch(QPainter &p, const Rect &r, const QColor &border,
                             const QColor &text, const QColor &dim,
                             const QColor &active_fill, const char *caption,
                             const char *a, const char *b, int active_idx,
                             bool enabled) {
  const QString label_texts[2] = {
      QString::fromUtf8(a ? a : ""),
      QString::fromUtf8(b ? b : "")};
  QColor edge = enabled ? border : dim;
  QColor t = enabled ? text : dim;
  QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);
  bool neutral = active_idx < 0;

  QFont old_font = p.font();
  QFont fcap = old_font;
  QString caption_text = QString::fromUtf8(caption ? caption : "");
  int caption_h =
      control_paint_clamp_int((int)std::lround((double)r.h * 0.36), 14, 24);
  const int min_seg_h =
      control_paint_clamp_int((int)std::lround((double)r.h * 0.48), 14, 26);
  int max_caption_h = std::max(12, r.h - min_seg_h);
  max_caption_h = std::min(max_caption_h, caption_h + 2);
  int caption_pt = control_paint_fit_text_point_size(
      old_font, caption_text, std::max(12, r.w - 12),
      std::max(10, max_caption_h - 2), 10, 18, false,
      control_paint_caption_text_scale());
  if (control_paint_uniform_text_pt() > 0)
    caption_pt = std::max(caption_pt, control_paint_uniform_text_pt());
  fcap.setPointSize(caption_pt);
  p.setFont(fcap);
  caption_h = std::max(caption_h, QFontMetrics(fcap).height() + 4);
  caption_h = control_paint_clamp_int(caption_h, 12, std::max(12, r.h - min_seg_h));

  QFont fopt_probe = old_font;
  int option_pt_probe = control_paint_scale_pt_with_factor(
      std::max(8, caption_pt - 1), control_paint_switch_option_text_scale());
  option_pt_probe = control_paint_clamp_int(option_pt_probe, 8, 16);
  fopt_probe.setPointSize(option_pt_probe);
  QFontMetrics fm_opt_probe(fopt_probe);
  int wanted_seg_h = control_paint_clamp_int(
      fm_opt_probe.height() + 12, 16, std::max(16, r.h - caption_h));
  int total_wanted_h = caption_h + wanted_seg_h;
  int top_pad = (r.h > total_wanted_h) ? ((r.h - total_wanted_h) / 2) : 0;
  int base_y = r.y + top_pad;

  const int seg_y = base_y + caption_h;
  const int seg_h =
      std::max(min_seg_h, std::min(wanted_seg_h, r.y + r.h - seg_y));
  const int seg_draw_h = std::max(12, (int)std::lround((double)seg_h * 0.8));
  const int seg_draw_y = seg_y + std::max(0, (seg_h - seg_draw_h) / 2);

  p.setPen(t);
  p.drawText(QRect(r.x, base_y + 2, std::max(12, r.w), std::max(10, caption_h - 4)),
             Qt::AlignVCenter | Qt::AlignLeft,
             p.fontMetrics().elidedText(caption_text, Qt::ElideRight,
                                        std::max(12, r.w)));

  QPainterPath path;
  int cl = 8;
  path.moveTo(r.x + cl, seg_draw_y);
  path.lineTo(r.x + r.w - cl, seg_draw_y);
  path.lineTo(r.x + r.w, seg_draw_y + cl);
  path.lineTo(r.x + r.w, seg_draw_y + seg_draw_h - cl);
  path.lineTo(r.x + r.w - cl, seg_draw_y + seg_draw_h);
  path.lineTo(r.x + cl, seg_draw_y + seg_draw_h);
  path.lineTo(r.x, seg_draw_y + seg_draw_h - cl);
  path.lineTo(r.x, seg_draw_y + cl);
  path.closeSubpath();

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(edge, 1));
  p.setBrush(QColor(10, 20, 30, 200));
  p.drawPath(path);

  int seg_w = r.w / 2;
  for (int i = 0; i < 2; ++i) {
    int sx = r.x + i * seg_w;
    int sw = (i == 1) ? (r.w - seg_w) : seg_w;
    QRect seg(sx, seg_draw_y, sw, seg_draw_h);

    if (!neutral && i == active_idx) {
      p.setPen(Qt::NoPen);
      p.setBrush(active);
      p.drawRect(sx + 4, seg_draw_y + seg_draw_h - 6, sw - 8, 4);
      p.setBrush(QColor(active.red(), active.green(), active.blue(), 60));
      p.drawRect(sx + 1, seg_draw_y + 1, sw - 2, seg_draw_h - 2);
    }
    if (i > 0) {
      p.setPen(QPen(QColor(edge.red(), edge.green(), edge.blue(), 100), 1));
      p.drawLine(sx, seg_draw_y + 6, sx, seg_draw_y + seg_draw_h - 6);
    }
    p.setPen(!neutral && i == active_idx ? QColor("#ffffff") : t);
    QFont f = p.font();
    f.setBold(!neutral && i == active_idx);
    p.setFont(f);
    QRect seg_text_rect = seg.adjusted(10, 3, -10, -3);
    int option_pt = control_paint_fit_text_point_size(
      old_font, label_texts[i],
        std::max(8, seg_text_rect.width()), std::max(8, seg_text_rect.height()), 8,
        control_paint_clamp_int(
            std::max(9, (int)std::lround((double)seg_draw_h * 0.46)), 9, 18),
        (!neutral && i == active_idx),
        control_paint_switch_option_text_scale());
    if (control_paint_uniform_text_pt() > 0)
      option_pt = std::max(option_pt, control_paint_uniform_text_pt());
    f.setPointSize(option_pt);
    p.setFont(f);
    p.drawText(seg_text_rect, Qt::AlignCenter,
               p.fontMetrics().elidedText(label_texts[i], Qt::ElideRight,
                                          std::max(8, seg_text_rect.width())));
    f.setBold(false);
    p.setFont(f);
  }
  p.setRenderHint(QPainter::Antialiasing, false);
  if (!enabled)
    control_paint_paint_disabled_overlay(p, r, 8);
  p.setFont(old_font);
}
