#include "gui/core/runtime/gui_screen_utils.h"

#include <QRect>

#include <algorithm>

QList<QScreen *> ordered_screens_left_to_right(const QList<QScreen *> &screens) {
  QList<QScreen *> ordered = screens;
  std::sort(ordered.begin(), ordered.end(), [](QScreen *a, QScreen *b) {
    const QRect ga = a ? a->availableGeometry() : QRect();
    const QRect gb = b ? b->availableGeometry() : QRect();
    if (ga.x() != gb.x())
      return ga.x() < gb.x();
    if (ga.y() != gb.y())
      return ga.y() < gb.y();
    const QString an = a ? a->name() : QString();
    const QString bn = b ? b->name() : QString();
    return an < bn;
  });
  return ordered;
}
