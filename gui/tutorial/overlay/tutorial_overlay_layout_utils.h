#ifndef TUTORIAL_OVERLAY_LAYOUT_UTILS_H
#define TUTORIAL_OVERLAY_LAYOUT_UTILS_H

#include <QPointF>
#include <QRectF>
#include <QString>

void tutorial_overlay_clamp_callout_box(QRectF *box, int win_width,
                                        int win_height);

void tutorial_overlay_push_box_outside_avoid(
    QRectF *box, const QRectF &avoid, const QPointF &preferred_dir,
    const QPointF &fallback_dir, double step_px, int max_tries, int win_width,
    int win_height);

void tutorial_overlay_apply_step2_special_avoidance(
    const QString &id, const QRectF &ray_rect, QRectF *box, int win_width,
    int win_height);

void tutorial_overlay_apply_step2_sky_pair_spacing(
    const QString &id, QRectF *box, int win_width, int win_height,
    bool has_other_box, const QRectF &other_box);

#endif
