#ifndef TUTORIAL_OVERLAY_MATH_UTILS_H
#define TUTORIAL_OVERLAY_MATH_UTILS_H

#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

#include <vector>

struct TutorialGalaxyCalloutDef;

double clamp01(double t);
double lerp(double a, double b, double t);
QRectF lerp_rect(const QRectF &a, const QRectF &b, double t);
QRectF fit_rect_center_scaled(const QRectF &r, double scale);
QPainterPath torn_bottom_clip_path(const QRectF &rect, double amp_px,
                                   double freq_cycles);
QPainterPath torn_top_clip_path(const QRectF &rect, double amp_px,
                                double freq_cycles);
QPixmap build_map_torn_composite(const QPixmap &map_snapshot);
QPointF rect_edge_anchor_toward(const QRectF &r, const QPointF &dst);
std::vector<QPointF>
calculateRadialPositions(const QPointF &center,
                        const std::vector<TutorialGalaxyCalloutDef> &callouts,
                        double radius_scale = 1.0);

#endif
