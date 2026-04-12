#include "gui/layout/panels/monitor_panel_utils.h"

#include <QFontMetrics>

#include <cmath>

QRect monitor_plot_rect(int panel_x, int panel_y, int panel_w, int panel_h) {
  const int left = 60;
  const int right = 14;
  const int top = 44;
  const int bottom = 36;
  return QRect(panel_x + left, panel_y + top, panel_w - left - right,
               panel_h - top - bottom);
}

QRect monitor_y_label_rect(const QRect &draw_rect, int y_center, int width,
                           int height, int gap) {
  int right = draw_rect.left() - gap;
  return QRect(right - width, y_center - height / 2, width, height);
}

void draw_monitor_x_labels(QPainter &p, const QRect &draw_rect, int y,
                           const char *l, const char *c, const char *r) {
  int seg_w = draw_rect.width() / 3;
  if (seg_w < 36)
    seg_w = 36;
  const QFontMetrics fm = p.fontMetrics();
  QString ls = fm.elidedText(QString::fromUtf8(l), Qt::ElideRight, seg_w - 2);
  QString cs = fm.elidedText(QString::fromUtf8(c), Qt::ElideRight, seg_w - 2);
  QString rs = fm.elidedText(QString::fromUtf8(r), Qt::ElideLeft, seg_w - 2);
  p.drawText(QRect(draw_rect.left(), y, seg_w, 14),
             Qt::AlignLeft | Qt::AlignVCenter, ls);
  p.drawText(QRect(draw_rect.center().x() - seg_w / 2, y, seg_w, 14),
             Qt::AlignHCenter | Qt::AlignVCenter, cs);
  p.drawText(QRect(draw_rect.right() - seg_w + 1, y, seg_w, 14),
             Qt::AlignRight | Qt::AlignVCenter, rs);
}

void draw_monitor_inner_grid(QPainter &p, const QRect &r, const QColor &border,
                             const QColor &grid, int x_div, int y_div) {
  if (r.width() < 8 || r.height() < 8)
    return;

  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(border, 1));
  p.drawRect(r.adjusted(0, 0, -1, -1));

  p.setPen(QPen(grid, 1));
  for (int i = 1; i < x_div; ++i) {
    int x = r.left() + (int)llround((double)i * (double)(r.width() - 1) /
                                    (double)x_div);
    p.drawLine(x, r.top(), x, r.bottom());
  }
  for (int i = 1; i < y_div; ++i) {
    int y = r.top() + (int)llround((double)i * (double)(r.height() - 1) /
                                   (double)y_div);
    p.drawLine(r.left(), y, r.right(), y);
  }
}
