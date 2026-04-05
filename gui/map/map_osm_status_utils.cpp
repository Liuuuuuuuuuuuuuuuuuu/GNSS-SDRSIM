#include "gui/map/map_osm_status_utils.h"

#include <QFont>
#include <QFontMetrics>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace {

int clamp_int_local(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

} // namespace

QString map_osm_current_text(bool receiver_valid, double receiver_lat,
                             double receiver_lon) {
  if (receiver_valid) {
    return QString("Current %1, %2")
        .arg(receiver_lat, 0, 'f', 6)
        .arg(receiver_lon, 0, 'f', 6);
  }
  return "Current N/A";
}

QString map_osm_llh_text(bool has_selected_llh, double selected_lat,
                         double selected_lon, double selected_h,
                         const QString &current_text) {
  if (has_selected_llh) {
    return QString("Start LLH %1, %2, %3 | %4")
        .arg(selected_lat, 0, 'f', 6)
        .arg(selected_lon, 0, 'f', 6)
        .arg(selected_h, 0, 'f', 1)
        .arg(current_text);
  }
  return current_text;
}

QString map_osm_zoom_text(int zoom, int zoom_base) {
  double zoom_factor = std::pow(2.0, (double)zoom - (double)zoom_base);
  return QString("OpenStreetMap (drag to pan, wheel to zoom) | Zoom %1")
      .arg(zoom_factor, 0, 'f', zoom_factor >= 1.0 ? 1 : 2);
}

QStringList map_osm_status_lines(const QString &zoom_text,
                                 bool tutorial_enabled,
                                 bool tutorial_overlay_visible,
                                 const QString &status_text,
                                 const QString &llh_text) {
  QStringList lines;
  lines << zoom_text;
  if (!tutorial_enabled && !tutorial_overlay_visible) {
    lines << "New user tip: click GUIDE OFF at top-right to open step-by-step tutorial";
  }
  if (!status_text.isEmpty()) {
    lines << status_text;
  }
  lines << llh_text;
  return lines;
}

void map_osm_draw_status_badges(QPainter &p, const QRect &panel,
                                const QRect &stop_btn_rect, bool running_ui,
                                const QStringList &lines) {
  QFont prev_font = p.font();
  QFont badge_font = prev_font;
  if (running_ui) {
    badge_font.setPointSize(
        clamp_int_local(std::max(8, badge_font.pointSize() - 2), 8, 12));
  }
  p.setFont(badge_font);

  QFontMetrics fm(p.font());
  const int pad_x = running_ui ? 6 : 8;
  const int pad_y = running_ui ? 2 : 4;
  const int line_gap = running_ui ? 4 : 6;
  int line_h = fm.height() + 2 * pad_y;
  int base_x = panel.x() + 10;
  int base_y = panel.y() + panel.height() - 10 - line_h;

  int badge_max_w = std::max(120, panel.width() - 24);
  if (running_ui && !stop_btn_rect.isEmpty()) {
    int safe_w = stop_btn_rect.left() - base_x - 12;
    if (safe_w > 80) {
      badge_max_w = std::min(badge_max_w, safe_w);
    }
  }

  auto draw_text_badge = [&](int x, int y, const QString &txt) {
    QString elided =
        fm.elidedText(txt, Qt::ElideRight, std::max(40, badge_max_w - 2 * pad_x));
    QRect r(x, y, fm.horizontalAdvance(elided) + 2 * pad_x, line_h);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(120, 130, 145, 140), 1));
    p.setBrush(QColor(255, 255, 255, 210));
    p.drawRoundedRect(r, 5, 5);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor("#0f172a"));
    p.drawText(r.adjusted(pad_x, 0, -pad_x, 0),
               Qt::AlignVCenter | Qt::AlignLeft, elided);
  };

  int first_y = base_y - (int(lines.size()) - 1) * (line_h + line_gap);
  for (int i = 0; i < lines.size(); ++i) {
    draw_text_badge(base_x, first_y + i * (line_h + line_gap), lines[i]);
  }

  p.setFont(prev_font);
}
