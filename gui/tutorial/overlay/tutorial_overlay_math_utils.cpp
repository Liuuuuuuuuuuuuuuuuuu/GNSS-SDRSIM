#include "gui/tutorial/overlay/tutorial_overlay_math_utils.h"

#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

double clamp01(double t) {
  if (t < 0.0) return 0.0;
  if (t > 1.0) return 1.0;
  return t;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

QRectF lerp_rect(const QRectF &a, const QRectF &b, double t) {
  return QRectF(lerp(a.x(), b.x(), t), lerp(a.y(), b.y(), t),
                lerp(a.width(), b.width(), t),
                lerp(a.height(), b.height(), t));
}

QRectF fit_rect_center_scaled(const QRectF &r, double scale) {
  if (r.isEmpty()) return r;
  const QPointF c = r.center();
  const double w = std::max(1.0, r.width() * scale);
  const double h = std::max(1.0, r.height() * scale);
  return QRectF(c.x() - w * 0.5, c.y() - h * 0.5, w, h);
}

QPainterPath torn_bottom_clip_path(const QRectF &rect, double amp_px,
                                   double freq_cycles) {
  QPainterPath path;
  if (rect.isEmpty()) return path;

  const double left = rect.left();
  const double right = rect.right();
  const double top = rect.top();
  const double base = rect.bottom() - std::max(2.0, amp_px * 0.55);
  const double w = std::max(1.0, rect.width());

  path.moveTo(left, top);
  path.lineTo(right, top);
  path.lineTo(right, base);
  for (int i = 60; i >= 0; --i) {
    const double t = (double)i / 60.0;
    const double x = left + w * t;
    const double y = base +
                     amp_px * (0.66 * std::sin(t * freq_cycles * 2.0 * M_PI + 0.35) +
                               0.34 * std::sin(t * (freq_cycles * 3.0) * 2.0 * M_PI +
                                               1.10));
    path.lineTo(x, y);
  }
  path.closeSubpath();
  return path;
}

QPainterPath torn_top_clip_path(const QRectF &rect, double amp_px,
                                double freq_cycles) {
  QPainterPath path;
  if (rect.isEmpty()) return path;

  const double left = rect.left();
  const double right = rect.right();
  const double bottom = rect.bottom();
  const double base = rect.top() + std::max(2.0, amp_px * 0.55);
  const double w = std::max(1.0, rect.width());

  path.moveTo(left, bottom);
  path.lineTo(right, bottom);
  path.lineTo(right, base);
  for (int i = 60; i >= 0; --i) {
    const double t = (double)i / 60.0;
    const double x = left + w * t;
    const double y = base +
                     amp_px * (0.64 * std::sin(t * freq_cycles * 2.0 * M_PI + 0.90) +
                               0.36 * std::sin(t * (freq_cycles * 2.7) * 2.0 * M_PI +
                                               0.15));
    path.lineTo(x, y);
  }
  path.closeSubpath();
  return path;
}

QPixmap build_map_torn_composite(const QPixmap &map_snapshot) {
  if (map_snapshot.isNull()) return QPixmap();
  const int w = std::max(16, map_snapshot.width());
  const int h = std::max(16, map_snapshot.height());
  const int out_h = std::max(20, h / 2);
  const int gap = std::max(10, std::min(22, out_h / 12));
  const int piece_h = std::max(6, (out_h - gap) / 2);

  const QRect src_top(0, 0, w, std::max(1, h / 4));
  const QRect src_bottom(0, std::max(0, h - std::max(1, h / 4)), w,
                         std::max(1, h / 4));
  const QRectF dst_top(0.0, 0.0, (double)w, (double)piece_h);
  const QRectF dst_bottom(0.0, (double)(piece_h + gap), (double)w,
                          (double)piece_h);

  QPixmap out(w, out_h);
  out.fill(Qt::transparent);

  QPainter qp(&out);
  qp.setRenderHint(QPainter::Antialiasing, true);

  const double amp = std::max(2.0, piece_h * 0.10);
  const QPainterPath clip_top = torn_bottom_clip_path(dst_top, amp, 4.2);
  const QPainterPath clip_bottom = torn_top_clip_path(dst_bottom, amp, 4.2);

  qp.save();
  qp.setClipPath(clip_top);
  qp.drawPixmap(dst_top.toRect(), map_snapshot, src_top);
  qp.restore();

  qp.save();
  qp.setClipPath(clip_bottom);
  qp.drawPixmap(dst_bottom.toRect(), map_snapshot, src_bottom);
  qp.restore();

  const QRect gap_rect(0, piece_h, w, gap);
  qp.fillRect(gap_rect, QColor(6, 10, 18, 230));

  qp.setPen(QPen(QColor(8, 14, 24, 170), 1));
  qp.drawPath(clip_top);
  qp.drawPath(clip_bottom);

  qp.setPen(QPen(QColor(255, 255, 255, 36), 1));
  qp.drawLine(2, piece_h - 1, w - 2, piece_h - 1);
  qp.drawLine(2, piece_h + gap, w - 2, piece_h + gap);

  qp.end();
  return out;
}

QPointF rect_edge_anchor_toward(const QRectF &r, const QPointF &dst) {
  if (r.isEmpty()) return dst;

  const QPointF c = r.center();
  const double dx = dst.x() - c.x();
  const double dy = dst.y() - c.y();
  if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6) {
    return QPointF(r.right(), c.y());
  }

  const double hw = std::max(1e-6, r.width() * 0.5);
  const double hh = std::max(1e-6, r.height() * 0.5);
  const double tx = std::abs(dx) < 1e-6 ? 1e18 : hw / std::abs(dx);
  const double ty = std::abs(dy) < 1e-6 ? 1e18 : hh / std::abs(dy);
  const double t = std::min(tx, ty);
  return QPointF(c.x() + dx * t, c.y() + dy * t);
}

std::vector<QPointF>
calculateRadialPositions(const QPointF &center,
                        const std::vector<TutorialGalaxyCalloutDef> &callouts,
                        double radius_scale) {
  std::vector<QPointF> out;
  out.reserve(callouts.size());
  const double rs = std::max(0.1, radius_scale);

  for (const TutorialGalaxyCalloutDef &def : callouts) {
    const double theta = def.angle_deg * 3.14159265358979323846 / 180.0;
    const double r = std::max(20.0, def.radius_px * rs);
    out.push_back(QPointF(center.x() + std::cos(theta) * r,
                          center.y() + std::sin(theta) * r));
  }
  return out;
}
