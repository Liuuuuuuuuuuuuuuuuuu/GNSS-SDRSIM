#ifndef GUI_PATH_RENDER_UTILS_H
#define GUI_PATH_RENDER_UTILS_H

#include <functional>
#include <vector>

#include <QPainterPath>
#include <QPoint>
#include <QRect>

#include "gui/geo/geo_io.h"

using OSMToScreenFn = std::function<bool(double, double, const QRect &, QPoint *)>;

bool gui_build_osm_painter_path(const std::vector<LonLat> &polyline,
                                double start_lat_deg,
                                double start_lon_deg,
                                double end_lat_deg,
                                double end_lon_deg,
                                const QRect &panel,
                                const OSMToScreenFn &to_screen,
                                QPainterPath *out_path);

#endif
