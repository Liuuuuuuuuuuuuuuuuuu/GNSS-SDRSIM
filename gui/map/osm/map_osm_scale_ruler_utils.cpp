#include "gui/map/osm/map_osm_scale_ruler_utils.h"

#include "gui/map/osm/map_osm_metrics_utils.h"
#include "gui/geo/osm_projection.h"

#include <cmath>

QRect map_osm_draw_scale_bar(QPainter &p, const MapOsmPanelInput &in) {
  if (in.panel.width() < 120 || in.panel.height() < 120)
    return QRect();

  const double center_lat_deg = osm_world_y_to_lat(in.osm_center_px_y, in.osm_zoom);
  const double world_size_px = osm_world_size_for_zoom(in.osm_zoom);
  const double m_per_px =
      std::cos(center_lat_deg * M_PI / 180.0) * (2.0 * M_PI * 6378137.0) /
      world_size_px;
  if (!std::isfinite(m_per_px) || m_per_px <= 0.0)
    return QRect();

  const double target_px = 120.0;
  const double raw_m = m_per_px * target_px;
  if (!std::isfinite(raw_m) || raw_m <= 0.0)
    return QRect();

  const double p10 = std::pow(10.0, std::floor(std::log10(raw_m)));
  const double candidates[] = {1.0 * p10, 2.0 * p10, 5.0 * p10, 10.0 * p10};
  double bar_m = candidates[0];
  for (double c : candidates) {
    if (c <= raw_m)
      bar_m = c;
  }
  if (bar_m <= 0.0)
    return QRect();

  const int bar_px = (int)std::lround(bar_m / m_per_px);
  if (bar_px < 24)
    return QRect();

  QString label;
  if (bar_m >= 1000.0) {
    const double km = bar_m * 0.001;
    int dec = 0;
    if (km < 10.0)
      dec = 2;
    else if (km < 100.0)
      dec = 1;
    label = QString("%1 km").arg(km, 0, 'f', dec);
  } else {
    label = QString("%1 m").arg((int)std::lround(bar_m));
  }

  const int left_pad = 16;
  const int bottom_pad = in.running_ui ? 104 : 78;
  const int base_x = in.panel.x() + left_pad;
  const int base_y = in.panel.y() + in.panel.height() - bottom_pad;

  const QFont old_font = p.font();
  QFont f = old_font;
  f.setPointSize(
      std::max(9, old_font.pointSize() > 0 ? old_font.pointSize() - 1 : 9));
  f.setBold(in.scale_ruler_enabled);
  p.setFont(f);
  QFontMetrics fm(f);
  const int label_h = fm.height();
  const int label_w = fm.horizontalAdvance(label);

  const QRect bg_rect(base_x - 8, base_y - label_h - 14,
                      std::max(bar_px + 16, label_w + 16), label_h + 18);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(in.scale_ruler_enabled ? QColor(127, 240, 189, 220)
                                       : QColor(180, 200, 220, 185),
                in.scale_ruler_enabled ? 2 : 1));
  p.setBrush(in.scale_ruler_enabled
                 ? QColor(8, 34, 28, 228)
                 : (in.dark_map_mode ? QColor(7, 15, 26, 210)
                                     : QColor(9, 20, 35, 210)));
  p.drawRoundedRect(bg_rect, 6, 6);

  const QColor scale_color =
      in.dark_map_mode ? QColor("#eef4ff") : QColor("#f4f8ff");
  p.setPen(QPen(scale_color, 2));
  p.drawLine(base_x, base_y - 8, base_x + bar_px, base_y - 8);
  p.drawLine(base_x, base_y - 12, base_x, base_y - 4);
  p.drawLine(base_x + bar_px, base_y - 12, base_x + bar_px, base_y - 4);

  p.setPen(scale_color);
  p.drawText(QRect(base_x, base_y - label_h - 12, std::max(bar_px, label_w),
                   label_h),
             Qt::AlignLeft | Qt::AlignVCenter, label);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setFont(old_font);
  return bg_rect;
}

void map_osm_draw_scale_ruler_overlay(QPainter &p,
                                      const MapOsmPanelInput &in) {
  if (!in.scale_ruler_enabled || !in.scale_ruler_has_start)
    return;

  if (!in.coord_to_screen)
    return;

  QPoint a;
  if (!in.coord_to_screen(in.scale_ruler_start_lat_deg,
                          in.scale_ruler_start_lon_deg, &a)) {
    return;
  }

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor("#7ff0bd"), 2));
  p.setBrush(QColor("#7ff0bd"));
  p.drawEllipse(a, 5, 5);

  if (in.scale_ruler_has_end) {
    QPoint b;
    if (in.coord_to_screen(in.scale_ruler_end_lat_deg,
                           in.scale_ruler_end_lon_deg, &b)) {
      QPen line_pen(QColor("#7ff0bd"), 2,
                    in.scale_ruler_end_fixed ? Qt::SolidLine : Qt::DashLine,
                    Qt::RoundCap, Qt::RoundJoin);
      p.setPen(line_pen);
      p.setBrush(Qt::NoBrush);
      p.drawLine(a, b);

      p.setPen(QPen(QColor("#7ff0bd"), 2));
      p.setBrush(QColor("#7ff0bd"));
      p.drawEllipse(b, 5, 5);

      const double d_m = map_osm_metrics::distance_m_for_display(
          in.scale_ruler_start_lat_deg, in.scale_ruler_start_lon_deg,
          in.scale_ruler_end_lat_deg, in.scale_ruler_end_lon_deg);
      if (std::isfinite(d_m) && d_m >= 0.0) {
        const double d_km = d_m * 0.001;
        int dec = 0;
        if (d_km < 10.0)
          dec = 2;
        else if (d_km < 100.0)
          dec = 1;
        const QString txt =
            (in.language == GuiLanguage::ZhTw)
                ? QString::fromUtf8("量測 %1 公里").arg(d_km, 0, 'f', dec)
                : QString("RULER %1 km").arg(d_km, 0, 'f', dec);

        QFont old_font = p.font();
        QFont f = old_font;
        f.setPointSize(std::max(
            9, old_font.pointSize() > 0 ? old_font.pointSize() - 1 : 9));
        p.setFont(f);
        QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(txt) + 14;
        const int h = fm.height() + 8;
        const QPoint m((a.x() + b.x()) / 2, (a.y() + b.y()) / 2);
        QRect tag(m.x() - w / 2, m.y() - h - 8, w, h);
        tag.translate(0, -4);
        tag = tag.intersected(in.panel.adjusted(6, 6, -6, -6));

        p.setPen(QPen(QColor(127, 240, 189, 220), 1));
        p.setBrush(QColor(8, 30, 24, 220));
        p.drawRoundedRect(tag, 6, 6);
        p.setPen(QColor("#dffff2"));
        p.drawText(tag, Qt::AlignCenter, txt);
        p.setFont(old_font);
      }
    }
  }
  p.setRenderHint(QPainter::Antialiasing, false);
}
