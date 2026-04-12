#include "gui/tutorial/overlay/tutorial_overlay_layout_utils.h"

#include <cmath>

namespace {

QPointF normalize_or_fallback(const QPointF &v, const QPointF &fallback) {
  const double v_len = std::hypot(v.x(), v.y());
  if (v_len > 1e-6) {
    return QPointF(v.x() / v_len, v.y() / v_len);
  }
  const double fb_len = std::hypot(fallback.x(), fallback.y());
  if (fb_len > 1e-6) {
    return QPointF(fallback.x() / fb_len, fallback.y() / fb_len);
  }
  return QPointF(1.0, 0.0);
}

} // namespace

void tutorial_overlay_clamp_callout_box(QRectF *box, int win_width,
                                        int win_height) {
  if (!box) return;
  if (box->left() < 10.0) box->moveLeft(10.0);
  if (box->top() < 10.0) box->moveTop(10.0);
  if (box->right() > win_width - 10.0) box->moveRight(win_width - 10.0);
  if (box->bottom() > win_height - 58.0) box->moveBottom(win_height - 58.0);
}

void tutorial_overlay_push_box_outside_avoid(
    QRectF *box, const QRectF &avoid, const QPointF &preferred_dir,
    const QPointF &fallback_dir, double step_px, int max_tries, int win_width,
    int win_height) {
  if (!box) return;
  const QPointF dir = normalize_or_fallback(preferred_dir, fallback_dir);
  int tries = 0;
  while (box->intersects(avoid) && tries < max_tries) {
    box->translate(dir.x() * step_px, dir.y() * step_px);
    tutorial_overlay_clamp_callout_box(box, win_width, win_height);
    ++tries;
  }
}

void tutorial_overlay_apply_step2_special_avoidance(
    const QString &id, const QRectF &ray_rect, QRectF *box, int win_width,
    int win_height) {
  if (!box) return;

  if (id == "osm_llh") {
    const QRectF avoid(
        std::max(10.0, ray_rect.left() - 10.0),
        std::min((double)win_height - 58.0, ray_rect.bottom() + 72.0),
        std::max(120.0, (double)win_width * 0.32), 120.0);
    int tries = 0;
    while (box->intersects(avoid) && tries < 32) {
      box->translate(-6.0, 8.0);
      if (box->left() < 10.0) box->moveLeft(10.0);
      if (box->bottom() > win_height - 58.0) box->moveBottom(win_height - 58.0);
      ++tries;
    }
    return;
  }

  if (id == "new_user") {
    const QRectF avoid(ray_rect.center().x() - 40.0, ray_rect.bottom() + 68.0,
                       std::max(140.0, (double)win_width * 0.38), 120.0);
    int tries = 0;
    while (box->intersects(avoid) && tries < 32) {
      box->translate(8.0, 8.0);
      if (box->right() > win_width - 10.0) box->moveRight(win_width - 10.0);
      if (box->bottom() > win_height - 58.0) box->moveBottom(win_height - 58.0);
      ++tries;
    }
    return;
  }

  if (id == "smart_path" || id == "straight_path" || id == "mouse_hint") {
    const QRectF avoid = ray_rect.adjusted(-16.0, -16.0, 16.0, 16.0);
    int tries = 0;
    while (box->intersects(avoid) && tries < 40) {
      if (id == "mouse_hint") {
        box->translate(12.0, 0.0);
        if (box->right() > win_width - 10.0) box->moveRight(win_width - 10.0);
      } else {
        box->translate(-12.0, 0.0);
        if (box->left() < 10.0) box->moveLeft(10.0);
      }
      ++tries;
    }
  }
}

void tutorial_overlay_apply_step2_sky_pair_spacing(
    const QString &id, QRectF *box, int win_width, int win_height,
    bool has_other_box, const QRectF &other_box) {
  if (!box) return;
  if ((id != "sky_g" && id != "sky_c") || !has_other_box) return;

  const double min_edge_gap = 16.0;
  const double min_center_gap = 120.0;
  auto too_close_to = [&](const QRectF &a, const QRectF &b) {
    const QRectF b_with_gap =
        b.adjusted(-min_edge_gap, -min_edge_gap, min_edge_gap, min_edge_gap);
    if (a.intersects(b_with_gap)) return true;
    const QPointF d = a.center() - b.center();
    return std::hypot(d.x(), d.y()) < min_center_gap;
  };

  const QPointF hint =
      (id == "sky_g")
          ? ((box->center().y() <= other_box.center().y()) ? QPointF(1.0, -0.8)
                                                            : QPointF(1.0, 0.8))
          : ((box->center().y() >= other_box.center().y()) ? QPointF(1.0, 0.8)
                                                            : QPointF(1.0, -0.8));
  const QPointF dir = normalize_or_fallback(hint, QPointF(1.0, 0.0));

  int guard = 0;
  while (too_close_to(*box, other_box) && guard < 80) {
    box->translate(dir.x() * 8.0, dir.y() * 8.0);
    tutorial_overlay_clamp_callout_box(box, win_width, win_height);
    ++guard;
  }
}
