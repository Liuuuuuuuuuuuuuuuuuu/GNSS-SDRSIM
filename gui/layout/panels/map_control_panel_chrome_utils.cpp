#include "gui/layout/panels/map_control_panel_chrome_utils.h"

#include <algorithm>
#include <cmath>

void map_control_draw_section_frame(QPainter &p, const QRect &r) {
  if (r.width() <= 8 || r.height() <= 8)
    return;
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor(186, 224, 255, 220), 2));
  p.setBrush(QColor(24, 56, 88, 120));
  p.drawRoundedRect(r, 8, 8);
  p.setPen(QPen(QColor(232, 243, 255, 190), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 7, 7);
  p.setRenderHint(QPainter::Antialiasing, false);
}

void map_control_draw_gear_button(QPainter &p, const QRect &r,
                                  const QColor &accent_color) {
  if (r.width() <= 4 || r.height() <= 4)
    return;

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor(186, 224, 255, 220), 1));
  p.setBrush(QColor(22, 38, 56, 210));
  p.drawRoundedRect(r, 4, 4);

  QPointF c = r.center();
  const double radius = (double)std::min(r.width(), r.height()) * 0.26;
  p.setPen(QPen(accent_color, 1.6));
  for (int i = 0; i < 8; ++i) {
    const double pi = 3.14159265358979323846;
    const double a = (pi * 2.0 * i) / 8.0;
    const QPointF p0(c.x() + std::cos(a) * (radius + 1.0),
                     c.y() + std::sin(a) * (radius + 1.0));
    const QPointF p1(c.x() + std::cos(a) * (radius + 4.0),
                     c.y() + std::sin(a) * (radius + 4.0));
    p.drawLine(p0, p1);
  }
  p.setBrush(Qt::NoBrush);
  p.drawEllipse(c, radius + 1.0, radius + 1.0);
  p.setBrush(accent_color);
  p.setPen(Qt::NoPen);
  p.drawEllipse(c, 2.0, 2.0);
  p.setRenderHint(QPainter::Antialiasing, false);
}
