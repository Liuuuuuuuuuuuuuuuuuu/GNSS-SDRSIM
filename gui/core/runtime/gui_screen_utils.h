#ifndef GUI_SCREEN_UTILS_H
#define GUI_SCREEN_UTILS_H

#include <QList>
#include <QScreen>

QList<QScreen *> ordered_screens_left_to_right(const QList<QScreen *> &screens);

#endif