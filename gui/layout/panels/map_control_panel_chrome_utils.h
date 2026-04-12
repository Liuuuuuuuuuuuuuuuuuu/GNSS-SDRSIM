#ifndef MAP_CONTROL_PANEL_CHROME_UTILS_H
#define MAP_CONTROL_PANEL_CHROME_UTILS_H

#include <QColor>
#include <QPainter>
#include <QRect>

void map_control_draw_section_frame(QPainter &p, const QRect &r);

void map_control_draw_gear_button(QPainter &p, const QRect &r,
                                  const QColor &accent_color);

#endif
