#ifndef CROSSBOW_OVERLAY_H
#define CROSSBOW_OVERLAY_H

#include <QPainter>
#include <QPoint>
#include <QRect>

// Parameters for the Crossbow geometric overlay renderer.
struct CrossbowOverlayParams {
    QRect   panel;
    int     osm_zoom        = 12;
    double  center_lat_deg  = 0.0;   // geo-lat of center point (for scale computation)
    QPoint  center_screen;           // center point in screen coordinates
    QPoint  mouse_screen;            // current mouse position in screen coordinates
    bool    has_mouse       = false;
    bool    has_forward_axis = false;
    double  forward_axis_rad = 0.0;  // explicit forward direction (screen-space rad)
    bool    has_nfz         = false;
    QPoint  nfz_screen;              // nearest NFZ center in screen coordinates
    bool    nfz_in_valid_range = false;
    double  nfz_dist_m = 0.0;
    double  nearest_reverse_dist_m = -1.0;
    // When false, the NFZ-out-of-range warning badge is suppressed (e.g. micro/macro sub-panels).
    bool    draw_oor_badge = true;
};

// Draw the three-phase Crossbow geometric overlay.
// Phase 1 : dynamic sector   (radius=10 m, opening ≤ 60°)
// Phase 2 : forward guidance path (50 m beyond arc mid-point)
// Phase 3 : NFZ association lines (dashed perpendicular + convergent lines)
// Non-proportional scaling ensures a minimum visual size at any zoom level.
void crossbow_draw_overlay(QPainter &p, const CrossbowOverlayParams &params);

// Draw just the NFZ-out-of-range warning badge centered on the given panel rect.
// Pass nearest_reverse_dist_m > 0 only when there IS a nearest zone to report.
void crossbow_draw_oor_badge(QPainter &p, const QRect &panel,
                             double nearest_reverse_dist_m);

// Draw only the tactical distance rings used by simplified Crossbow mode.
// Keeps 10m and 50m circles visible without direction/NFZ geometry.
void crossbow_draw_range_rings(QPainter &p, const QRect &panel,
                               int osm_zoom, double center_lat_deg,
                               const QPoint &center_screen);

// Draw the "click to set center" hint badge when no center point has been selected.
void crossbow_draw_hint(QPainter &p, const QRect &panel);

#endif // CROSSBOW_OVERLAY_H
