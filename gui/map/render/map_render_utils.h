#ifndef MAP_RENDER_UTILS_H
#define MAP_RENDER_UTILS_H

#include <QPainter>
#include <QRect>

#include <vector>

#include "gui/geo/geo_io.h"

void map_draw_poly(QPainter &p, const LonLat *poly, int n, const QRect &r);
void map_draw_shp_land(QPainter &p, const std::vector<std::vector<LonLat>> &parts, const QRect &r);
void map_draw_ticks(QPainter &p, const QRect &r);

#endif
