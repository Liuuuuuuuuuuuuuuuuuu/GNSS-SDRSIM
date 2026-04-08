#include "gui/map/map_osm_status_utils.h"

#include "gui/core/gui_i18n.h"

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
                             double receiver_lon, GuiLanguage language) {
  if (receiver_valid) {
    return gui_i18n_text(language, "status.current")
        .arg(receiver_lat, 0, 'f', 6)
        .arg(receiver_lon, 0, 'f', 6);
  }
  return gui_i18n_text(language, "status.current_na");
}

QString map_osm_llh_text(bool has_selected_llh, double selected_lat,
                         double selected_lon, double selected_h,
                         const QString &current_text,
                         GuiLanguage language) {
  Q_UNUSED(has_selected_llh);
  Q_UNUSED(selected_lat);
  Q_UNUSED(selected_lon);
  Q_UNUSED(selected_h);
  Q_UNUSED(language);
  return current_text;
}

QString map_osm_zoom_text(int zoom, int zoom_base, GuiLanguage language) {
  double zoom_factor = std::pow(2.0, (double)zoom - (double)zoom_base);
  return gui_i18n_text(language, "status.zoom")
      .arg(zoom_factor, 0, 'f', zoom_factor >= 1.0 ? 1 : 2);
}

QStringList map_osm_status_lines(const QString &zoom_text,
                                 bool tutorial_enabled,
                                 bool tutorial_overlay_visible,
                                 const QString &status_text,
                                 const QString &llh_text,
                                 GuiLanguage language) {
  Q_UNUSED(zoom_text);
  Q_UNUSED(tutorial_enabled);
  Q_UNUSED(tutorial_overlay_visible);
  Q_UNUSED(status_text);
  Q_UNUSED(language);

  QStringList lines;
  if (!llh_text.isEmpty()) {
    lines << llh_text;
  }
  return lines;
}

void map_osm_draw_status_badges(QPainter &p, const QRect &panel,
                                const QRect &stop_btn_rect, bool running_ui,
                                const QStringList &lines,
                                std::vector<QRect> *badge_rects) {
  QFont prev_font = p.font();
  QFont badge_font = prev_font;
  int badge_pt = badge_font.pointSize();
  if (badge_pt <= 0)
    badge_pt = 10;
  badge_pt -= running_ui ? 2 : 1;
  badge_font.setPointSize(clamp_int_local(badge_pt, 8, 11));
  p.setFont(badge_font);

  QFontMetrics fm(p.font());
  const int pad_x = running_ui ? 5 : 6;
  const int pad_y = running_ui ? 2 : 3;
  const int line_gap = running_ui ? 3 : 4;
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
    if (badge_rects) {
      badge_rects->push_back(r);
    }
    
    // 使用按鈕風格繪製徽章
    p.setRenderHint(QPainter::Antialiasing, true);
    
    // 創建漸變背景（藍色）
    QLinearGradient grad(r.x(), r.y(), r.x(), r.y() + r.height());
    grad.setColorAt(0.0, QColor(28, 48, 75, 220));  // 淺藍色
    grad.setColorAt(1.0, QColor(18, 28, 45, 230));  // 深藍色
    
    // 邊框顏色
    QColor border_color(80, 120, 160, 160);
    
    p.setPen(QPen(border_color, 1));
    p.setBrush(grad);
    p.drawRoundedRect(r, 6, 6);  // 使用較大的圓角像按鈕
    
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor("#c4d2e4"));  // 淺色文字，像按鈕上的文字
    p.drawText(r.adjusted(pad_x, 0, -pad_x, 0),
               Qt::AlignVCenter | Qt::AlignLeft, elided);
  };

  int first_y = base_y - (int(lines.size()) - 1) * (line_h + line_gap);
  for (int i = 0; i < lines.size(); ++i) {
    draw_text_badge(base_x, first_y + i * (line_h + line_gap), lines[i]);
  }

  p.setFont(prev_font);
}
