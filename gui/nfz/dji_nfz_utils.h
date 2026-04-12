#ifndef DJI_NFZ_UTILS_H
#define DJI_NFZ_UTILS_H

#include "gui/nfz/dji_nfz.h"

#include <QJsonObject>
#include <QPainterPath>
#include <QRect>

#include <functional>
#include <vector>

int dji_nfz_draw_weight(const DjiNfzZone &zone);
void dji_nfz_layer_colors(int layer, QColor *stroke, QColor *fill);
double dji_nfz_zone_area_score(const DjiNfzZone &z);

void dji_nfz_parse_zone_geometry(const QJsonObject &src, const QString &zone_name,
                                 int fallback_level,
                                 std::vector<DjiNfzZone> *out_zones);

void dji_nfz_add_ring_to_path(
    QPainterPath &path, const DjiPolygonRing &ring,
    std::function<bool(double, double, QPoint *)> coord_to_screen_fn,
    bool smooth_edges);

bool dji_nfz_looks_like_outer_frame(
    const DjiNfzZone &z, const QRect &panel,
    std::function<bool(double, double, QPoint *)> coord_to_screen_fn);

#endif
