#include "gui/map/map_render_utils.h"

#include <QPainterPath>
#include <QPoint>
#include <QPolygon>

#include <cmath>
#include <cstdio>

void map_draw_poly(QPainter &p, const LonLat *poly, int n, const QRect &r)
{
    if (n < 3) return;
    QPolygon qp;
    qp.reserve(n);
    for (int i = 0; i < n; ++i) {
        qp.append(QPoint(r.x() + lon_to_x(poly[i].lon, r.width()), r.y() + lat_to_y(poly[i].lat, r.height())));
    }
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(qp);
}

void map_draw_shp_land(QPainter &p, const std::vector<std::vector<LonLat>> &parts, const QRect &r)
{
    for (const auto &ring : parts) {
        if (ring.size() < 2) continue;

        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        bool has_seg = false;
        QPoint last;

        for (size_t j = 0; j < ring.size(); ++j) {
            QPoint cur(r.x() + lon_to_x(ring[j].lon, r.width()), r.y() + lat_to_y(ring[j].lat, r.height()));
            if (!has_seg) {
                path.moveTo(cur);
                has_seg = true;
                last = cur;
                continue;
            }

            double dlon = std::fabs(ring[j].lon - ring[j - 1].lon);
            int dx = std::abs(cur.x() - last.x());
            if (dlon > 180.0 || dx > r.width() / 2) {
                path.moveTo(cur);
            } else {
                path.lineTo(cur);
            }
            last = cur;
        }
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
}

void map_draw_ticks(QPainter &p, const QRect &r)
{
    for (int lon = -180; lon <= 180; lon += 60) {
        if (lon == 180) continue;
        int x = r.x() + lon_to_x((double)lon, r.width());
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%d", lon);
        int x_off = -10;
        if (lon <= -180) x_off = 6;
        else if (lon >= 180) x_off = -34;
        p.drawText(x + x_off, r.y() + 14, lbl);
    }

    for (int lat = -60; lat <= 60; lat += 30) {
        int y = r.y() + lat_to_y((double)lat, r.height());
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%d", lat);
        p.drawText(r.x() + 8, y - 4, lbl);
    }
}
