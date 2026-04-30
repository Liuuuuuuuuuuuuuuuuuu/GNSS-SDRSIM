#include "gui/nfz/dji_nfz.h"
#include "gui/nfz/dji_nfz_utils.h"
#include "gui/nfz/nfz_hit_test_utils.h"

#include <QColor>

#include <array>
#include <algorithm>
#include <cmath>

void dji_nfz_draw(QPainter &p, const QRect &panel,
                  const std::vector<DjiNfzZone> &zones, int zoom,
                  std::function<bool(double, double, QPoint *)> coord_to_screen_fn,
                  std::array<bool, 4> *out_rendered_layers) {
  if (out_rendered_layers) {
    out_rendered_layers->fill(false);
  }

  std::vector<const DjiNfzZone *> sorted_zones;
  sorted_zones.reserve(zones.size());
  for (const auto &zone : zones) {
    sorted_zones.push_back(&zone);
  }

  std::sort(sorted_zones.begin(), sorted_zones.end(),
            [](const DjiNfzZone *a, const DjiNfzZone *b) {
              const int wa = dji_nfz_draw_weight(*a);
              const int wb = dji_nfz_draw_weight(*b);
              if (wa != wb)
                return wa < wb;
              return dji_nfz_zone_area_score(*a) > dji_nfz_zone_area_score(*b);
            });

  const bool smooth_edges = (zoom >= 10);
  for (const DjiNfzZone *nfz : sorted_zones) {
    if (!nfz) {
      continue;
    }
    if (dji_nfz_looks_like_outer_frame(*nfz, panel, coord_to_screen_fn)) {
      continue;
    }
    const int layer_idx = nfz_zone_layer_index(*nfz);
    if (out_rendered_layers && layer_idx >= 0 && layer_idx < 4) {
      (*out_rendered_layers)[layer_idx] = true;
    }

    QColor stroke;
    QColor fill;
    dji_nfz_layer_colors(layer_idx, &stroke, &fill);
    const QColor dji_color(nfz->color_hex);
    if (dji_color.isValid()) {
      stroke = dji_color;
      stroke.setAlpha(235);
      fill = dji_color;
      fill.setAlpha(96);
    }
    p.setPen(QPen(stroke, 2, Qt::SolidLine));
    p.setBrush(QBrush(fill));

    if (nfz->type == DjiNfzType::POLYGON) {
      QPainterPath path;
      path.setFillRule(Qt::OddEvenFill);

      for (const auto &ring : nfz->rings) {
        dji_nfz_add_ring_to_path(path, ring, coord_to_screen_fn, smooth_edges);
      }

      if (!path.isEmpty()) {
        p.drawPath(path);
      }
    } else if (nfz->type == DjiNfzType::CIRCLE) {
      QPoint center_pt;
      if (coord_to_screen_fn(nfz->center_lat, nfz->center_lon, &center_pt)) {
        double m_per_px = 156543.03392 *
                          std::cos(nfz->center_lat * M_PI / 180.0) /
                          std::pow(2, zoom);
        int r_px = (int)std::round(nfz->radius_m / m_per_px);
        if (r_px < 3)
          r_px = 3;

        p.drawEllipse(center_pt, r_px, r_px);
      }
    }
  }
}
