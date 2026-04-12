#include "gui/control/panel/control_paint.h"
#include "gui/control/panel/control_paint_internal.h"

#include <QPainterPath>
#include <QString>

#include <algorithm>
#include <cmath>

void control_draw_button(QPainter &p, const Rect &r, const QColor &border,
                         const QColor &text, const char *label) {
  QFont old_font = p.font();
  const QString label_text = QString::fromUtf8(label ? label : "");
  const QRect text_rect(r.x + 6, r.y + 2, std::max(4, r.w - 12),
                        std::max(4, r.h - 4));
  int fitted_pt = control_paint_fit_text_point_size(
      old_font, label_text, text_rect.width(), text_rect.height(), 8,
      control_paint_clamp_int(std::max(9, (int)std::round(r.h * 0.44)), 9, 16));
  QFont f = old_font;
  f.setPointSize(fitted_pt);
  p.setFont(f);
  QPainterPath path;
  int cl = 6;
  path.moveTo(r.x + cl, r.y);
  path.lineTo(r.x + r.w, r.y);
  path.lineTo(r.x + r.w, r.y + r.h - cl);
  path.lineTo(r.x + r.w - cl, r.y + r.h);
  path.lineTo(r.x, r.y + r.h);
  path.lineTo(r.x, r.y + cl);
  path.closeSubpath();

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(border, 1));
  p.setBrush(QColor(18, 28, 45, 210));
  p.drawPath(path);
  p.setPen(text);
  const QString elided =
      p.fontMetrics().elidedText(label_text, Qt::ElideRight, text_rect.width());
  p.drawText(text_rect, Qt::AlignCenter, elided);
  p.setFont(old_font);
  p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_button_filled(QPainter &p, const Rect &r, const QColor &fill,
                                const QColor &border, const QColor &text,
                                const char *label) {
  QFont old_font = p.font();
  const QString label_text = QString::fromUtf8(label ? label : "");
  const QRect text_rect(r.x + 7, r.y + 2, std::max(4, r.w - 14),
                        std::max(4, r.h - 4));
  QPainterPath path;
  int cl = 10;
  path.moveTo(r.x + cl, r.y);
  path.lineTo(r.x + r.w, r.y);
  path.lineTo(r.x + r.w, r.y + r.h - cl);
  path.lineTo(r.x + r.w - cl, r.y + r.h);
  path.lineTo(r.x, r.y + r.h);
  path.lineTo(r.x, r.y + cl);
  path.closeSubpath();

  QLinearGradient grad(r.x, r.y, r.x, r.y + r.h);
  grad.setColorAt(0.0, fill.lighter(115));
  grad.setColorAt(1.0, fill.darker(115));

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(border, 1));
  p.setBrush(grad);
  p.drawPath(path);

  p.setPen(text);
  QFont f = p.font();
  int fitted_pt = control_paint_fit_text_point_size(
      old_font, label_text, text_rect.width(), text_rect.height(), 8,
      control_paint_clamp_int(std::max(9, (int)std::round(r.h * 0.44)), 9, 16),
      true);
  f.setPointSize(fitted_pt);
  f.setLetterSpacing(QFont::PercentageSpacing, 104);
  p.setFont(f);
  const QString elided =
      p.fontMetrics().elidedText(label_text, Qt::ElideRight, text_rect.width());
  p.drawText(text_rect, Qt::AlignCenter, elided);
  f = old_font;
  f.setLetterSpacing(QFont::PercentageSpacing, 100);
  p.setFont(f);
  p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_checkbox(QPainter &p, const Rect &r, const QColor &border,
                           const QColor &text, const QColor &dim,
                           const char *label, bool on, bool enabled) {
  QColor edge = enabled ? border : dim;
  QColor t = enabled ? text : dim;
  int box_size =
      control_paint_clamp_int((int)std::round((double)r.h * 0.62), 14, 20);
  QRect box(r.x, r.y + (r.h - box_size) / 2, box_size, box_size);

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(edge, 1));
  p.setBrush(QColor(10, 20, 30, 200));
  p.drawRect(box);

  if (on) {
    p.setPen(QPen(QColor("#00ffcc"), 3));
    p.drawLine(box.x() + 4, box.y() + 9, box.x() + 8, box.y() + 13);
    p.drawLine(box.x() + 8, box.y() + 13, box.x() + 14, box.y() + 4);
  }
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(t);
  QFont old_font = p.font();
  QFont f = old_font;
  int label_w = std::max(8, r.w - box_size - 10);
  int label_h = std::max(8, r.h - 2);
  int fitted_pt = control_paint_fit_text_point_size(
      old_font, QString::fromUtf8(label ? label : ""), label_w, label_h, 10,
      control_paint_clamp_int(std::max(11, (int)std::round(r.h * 0.62)), 11,
                              22));
  if (control_paint_uniform_label_pt() > 0)
    fitted_pt = control_paint_uniform_label_pt();
  if (control_paint_uniform_text_pt() > 0)
    fitted_pt = std::max(fitted_pt, control_paint_uniform_text_pt());
  f.setPointSize(fitted_pt);
  p.setFont(f);
  QRect label_rect(r.x + box_size + 10, r.y, std::max(0, r.w - box_size - 10),
                   r.h);
  p.drawText(label_rect, Qt::AlignVCenter | Qt::AlignLeft,
             p.fontMetrics().elidedText(QString::fromUtf8(label ? label : ""),
                                        Qt::ElideRight, label_rect.width()));
  if (!enabled)
    control_paint_paint_disabled_overlay(p, r, 6);
  p.setFont(old_font);
}

void control_draw_text_right(QPainter &p, int right_x, int baseline_y,
                             const char *text) {
  if (!text)
    return;
  int w = p.fontMetrics().horizontalAdvance(text);
  p.drawText(right_x - w, baseline_y, text);
}
