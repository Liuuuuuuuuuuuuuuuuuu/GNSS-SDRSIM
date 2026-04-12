#ifndef MONITOR_PANEL_UTILS_H
#define MONITOR_PANEL_UTILS_H

#include <QPainter>
#include <QRect>

QRect monitor_plot_rect(int panel_x, int panel_y, int panel_w, int panel_h);
QRect monitor_y_label_rect(const QRect &draw_rect, int y_center,
                           int width = 54, int height = 16, int gap = 4);
void draw_monitor_x_labels(QPainter &p, const QRect &draw_rect, int y,
                           const char *l, const char *c, const char *r);
void draw_monitor_inner_grid(QPainter &p, const QRect &r, const QColor &border,
                             const QColor &grid, int x_div, int y_div);

#endif