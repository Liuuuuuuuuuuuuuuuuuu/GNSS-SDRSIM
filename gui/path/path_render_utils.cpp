#include "gui/path/path_render_utils.h"

bool gui_build_osm_painter_path(const std::vector<LonLat> &polyline,
                                double start_lat_deg,
                                double start_lon_deg,
                                double end_lat_deg,
                                double end_lon_deg,
                                const QRect &panel,
                                const OSMToScreenFn &to_screen,
                                QPainterPath *out_path)
{
    if (!out_path || !to_screen) {
        return false;
    }

    QPainterPath path;
    bool has_path = false;
    if (polyline.size() >= 2) {
        for (const auto &pt_ll : polyline) {
            QPoint pt;
            if (!to_screen(pt_ll.lat, pt_ll.lon, panel, &pt)) {
                continue;
            }
            if (!has_path) {
                path.moveTo(pt);
                has_path = true;
            } else {
                path.lineTo(pt);
            }
        }
    }

    if (!has_path) {
        QPoint a;
        QPoint b;
        if (to_screen(start_lat_deg, start_lon_deg, panel, &a) &&
            to_screen(end_lat_deg, end_lon_deg, panel, &b)) {
            path.moveTo(a);
            path.lineTo(b);
            has_path = true;
        }
    }

    if (has_path) {
        *out_path = path;
    }
    return has_path;
}
