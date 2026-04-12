#ifndef TUTORIAL_OVERLAY_RENDER_UTILS_H
#define TUTORIAL_OVERLAY_RENDER_UTILS_H

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QRect>
#include <QRectF>

#include <vector>

#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

bool tutorial_overlay_overlaps_existing_box(
    const QRectF &candidate, const std::vector<QRectF> &placed_boxes,
    int step);

void tutorial_overlay_resolve_overlap_with_placed(
    QRectF *box, const std::vector<QRectF> &placed_boxes, const QRectF &ray_rect,
    int step, int win_width, int win_height);

QPointF tutorial_overlay_draw_callout_connector(
    QPainter &p, const QRectF &ray_rect, const QRectF &box,
    const QPointF &anchor_override, bool use_anchor_override, int step,
    double pulse);

void tutorial_overlay_adjust_callout_box_height_for_text(
    QRectF *box, const TutorialGalaxyCalloutDef &def, const QFont &text_font,
    int win_height);

void tutorial_overlay_draw_callout_box_and_text(
    QPainter &p, const QRectF &box, const TutorialGalaxyCalloutDef &def,
    const QFont &text_font);

void tutorial_overlay_draw_nav_buttons(QPainter &p, const TutorialOverlayInput &in,
                                       const TutorialOverlayState &state,
                                       int step);

#endif
