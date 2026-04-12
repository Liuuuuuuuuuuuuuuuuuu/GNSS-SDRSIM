#ifndef MAP_OSM_STATUS_UTILS_H
#define MAP_OSM_STATUS_UTILS_H

#include <QPainter>
#include <QRect>
#include <QString>
#include <QStringList>

#include <vector>

void map_osm_draw_status_badges(QPainter &p, const QRect &panel,
                                const QRect &stop_btn_rect, bool running_ui,
                                const QStringList &lines,
                                std::vector<QRect> *badge_rects = nullptr);

#endif