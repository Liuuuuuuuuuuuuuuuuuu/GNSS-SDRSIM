#include "gui/map/overlay/crossbow_overlay.h"

#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QPen>
#include <QBrush>

#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// Physical constants
// ──────────────────────────────────────────────────────────────────────────────
static const double CBW_PI             = M_PI;
static const double CBW_EARTH_CIRC_M   = 40075016.0;  // equatorial circumference [m]
static const double CBW_PHYS_RADIUS_M  = 10.0;        // physical sector radius   [m]
static const double CBW_PHYS_PATH_M    = 50.0;        // physical forward path    [m]
static const double CBW_MIN_VIS_PX     = 5.0;         // minimum screen size      [px]

static const QColor CBW_50M_FILL(58, 16, 82, 58);
static const QColor CBW_50M_OUTER(188, 116, 255, 242);
static const QColor CBW_50M_OUTER_GLOW(118, 52, 176, 145);
static const QColor CBW_10M_FILL(96, 12, 22, 84);
static const QColor CBW_10M_OUTER(244, 90, 110, 246);
static const QColor CBW_10M_OUTER_GLOW(146, 24, 42, 165);

void crossbow_draw_range_rings(QPainter &p, const QRect &panel,
                               int osm_zoom, double center_lat_deg,
                               const QPoint &center_screen)
{
    const double cx = static_cast<double>(center_screen.x());
    const double cy = static_cast<double>(center_screen.y());

    const double lat_rad = center_lat_deg * CBW_PI / 180.0;
    const double meters_per_pixel =
        CBW_EARTH_CIRC_M * std::cos(lat_rad) /
        (256.0 * static_cast<double>(1u << osm_zoom));
    const double pixels_per_meter =
        (meters_per_pixel > 1e-12) ? 1.0 / meters_per_pixel : 1.0;

    const double r10 = std::max(CBW_MIN_VIS_PX, 10.0 * pixels_per_meter);
    const double r50 = std::max(CBW_MIN_VIS_PX, 50.0 * pixels_per_meter);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(panel);

    p.setPen(QPen(CBW_50M_OUTER_GLOW, 3.2));
    p.setBrush(QColor(CBW_50M_FILL.red(), CBW_50M_FILL.green(), CBW_50M_FILL.blue(), 36));
    p.drawEllipse(QPointF(cx, cy), r50, r50);
    p.setPen(QPen(CBW_50M_OUTER, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), r50, r50);

    p.setPen(QPen(CBW_10M_OUTER_GLOW, 3.6));
    p.setBrush(QColor(CBW_10M_FILL.red(), CBW_10M_FILL.green(), CBW_10M_FILL.blue(), 48));
    p.drawEllipse(QPointF(cx, cy), r10, r10);
    p.setPen(QPen(CBW_10M_OUTER, 2.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), r10, r10);

    QFont lbl_font;
    lbl_font.setPixelSize(11);
    lbl_font.setBold(true);
    p.setFont(lbl_font);
    p.setPen(QColor(200, 240, 255, 215));
    p.drawText(QPointF(cx + r10 + 6.0, cy - 3.0), "10m");
    p.drawText(QPointF(cx + r50 + 6.0, cy - 3.0), "50m");

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 80, 80, 230));
    p.drawEllipse(QPointF(cx, cy), 4.2, 4.2);

    p.restore();
}

// ──────────────────────────────────────────────────────────────────────────────
// crossbow_draw_overlay
// ──────────────────────────────────────────────────────────────────────────────
void crossbow_draw_overlay(QPainter &p, const CrossbowOverlayParams &parm)
{
    const double cx = static_cast<double>(parm.center_screen.x());
    const double cy = static_cast<double>(parm.center_screen.y());

    // ── Meters-per-pixel at current zoom / latitude ──────────────────────────
    // 1 OSM pixel at zoom z, latitude φ  ≈  (C · cos φ) / (256 · 2^z)
    const double lat_rad = parm.center_lat_deg * CBW_PI / 180.0;
    const double meters_per_pixel =
        CBW_EARTH_CIRC_M * std::cos(lat_rad) /
        (256.0 * static_cast<double>(1u << parm.osm_zoom));
    const double pixels_per_meter =
        (meters_per_pixel > 1e-12) ? 1.0 / meters_per_pixel : 1.0;

    // ── Non-proportional visual sizes ────────────────────────────────────────
    // When the physical extent is smaller than CBW_MIN_VIS_PX (county-level
    // zoom), the geometry is rendered at the minimum visual size so the event
    // remains perceptible while the geographic relationship is still expressed.
    const double visual_radius_px =
        std::max(CBW_MIN_VIS_PX, CBW_PHYS_RADIUS_M * pixels_per_meter);
    const double visual_path_px =
        std::max(CBW_MIN_VIS_PX, CBW_PHYS_PATH_M   * pixels_per_meter);

    const double r10 = std::max(CBW_MIN_VIS_PX, 10.0 * pixels_per_meter);
    const double r25 = std::max(CBW_MIN_VIS_PX, 25.0 * pixels_per_meter);
    const double r50 = std::max(CBW_MIN_VIS_PX, 50.0 * pixels_per_meter);
    const double r100 = std::max(CBW_MIN_VIS_PX, 100.0 * pixels_per_meter);

    // ── Forward direction (screen-space angle, y increases downward) ─────────
    double fwd_angle    = 0.0;   // radians; 0 = rightward (east in typical view)
    if (parm.has_forward_axis) {
        fwd_angle = parm.forward_axis_rad;
    }
    if (parm.has_mouse) {
        const double dx = static_cast<double>(parm.mouse_screen.x()) - cx;
        const double dy = static_cast<double>(parm.mouse_screen.y()) - cy;
        const double dist_to_mouse = std::sqrt(dx * dx + dy * dy);
        if (!parm.has_forward_axis && dist_to_mouse > 2.0) {
            fwd_angle = std::atan2(dy, dx);
        }
    }

    // ── Rear NFZ fan half-angle (120° total fan width) ──────────────────────
    const double half_angle_deg = 60.0;
    const double half_angle     = half_angle_deg * CBW_PI / 180.0;
    const double r              = visual_radius_px;

    // ── Forward edge midpoint and path tip ────────────────────────────────────
    const QPointF arc_mid(cx + r * std::cos(fwd_angle), cy + r * std::sin(fwd_angle));

    // Forward-path guidance tip (Phase 2)
    const QPointF path_tip(
        arc_mid.x() + visual_path_px * std::cos(fwd_angle),
        arc_mid.y() + visual_path_px * std::sin(fwd_angle));

    // ─────────────────────────────────────────────────────────────────────────
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(parm.panel);

    // Tactical distance rings for map-side marking (10m / 25m / 50m / 100m).
    auto draw_range_ring = [&](double rr, const QColor &col, Qt::PenStyle style,
                               const char *label) {
        if (rr < 2.0) return;
        QPen pen(col, 1.2);
        pen.setStyle(style);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), rr, rr);
        p.setPen(QColor(170, 240, 190, 190));
        p.drawText(QPointF(cx + rr + 4.0, cy - 3.0), label);
    };
    draw_range_ring(r10,  QColor(70, 230, 120, 220), Qt::SolidLine, "10m");
    draw_range_ring(r25,  QColor(160, 240, 100, 200), Qt::DashLine, "25m");
    draw_range_ring(r50,  QColor(240, 220, 90, 180), Qt::DotLine,  "50m");
    draw_range_ring(r100, QColor(250, 140, 100, 160), Qt::DotLine,  "100m");

    // ── Phase 0: 50m circle (used as the tangent base circle) ───────────────
    const double r50_cbw = r + visual_path_px;  // crossbow geometric extent
    QPainterPath circle50_path;
    circle50_path.addEllipse(QPointF(cx, cy), r50_cbw, r50_cbw);
    p.fillPath(circle50_path, CBW_50M_FILL);
    p.setPen(QPen(CBW_50M_OUTER_GLOW, 4.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(circle50_path);
    p.setPen(QPen(CBW_50M_OUTER, 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(circle50_path);

    // ── Phase 1: 10m circle ──────────────────────────────────────────────────
    QPainterPath circle_path;
    circle_path.addEllipse(QPointF(cx, cy), r, r);
    p.fillPath(circle_path, CBW_10M_FILL);
    p.setPen(QPen(CBW_10M_OUTER_GLOW, 4.8));
    p.setBrush(Qt::NoBrush);
    p.drawPath(circle_path);
    p.setPen(QPen(CBW_10M_OUTER, 2.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(circle_path);

    // ── Phase 2: forward guidance path + arrowhead ───────────────────────────
    p.setPen(QPen(QColor(224, 146, 80, 235), 2.0));
    p.drawLine(arc_mid, path_tip);

    // Arrowhead: two wing lines converging at path_tip
    const double arr_len    = std::min(12.0, visual_path_px * 0.25 + 6.0);
    const double arr_spread = 0.42;   // radians (~24°)
    const QPointF arr_l(
        path_tip.x() - arr_len * std::cos(fwd_angle - arr_spread),
        path_tip.y() - arr_len * std::sin(fwd_angle - arr_spread));
    const QPointF arr_r(
        path_tip.x() - arr_len * std::cos(fwd_angle + arr_spread),
        path_tip.y() - arr_len * std::sin(fwd_angle + arr_spread));
    p.drawLine(path_tip, arr_l);
    p.drawLine(path_tip, arr_r);

    // ── Phase 3: NFZ association lines ───────────────────────────────────────
    // Lines start from the actual sector arc boundary points (arc_left/arc_right
    // based on fwd_angle) so the entire geometry moves as one unit with the mouse.
    if (parm.has_nfz) {
        const QPointF nfz_pt(static_cast<double>(parm.nfz_screen.x()),
                             static_cast<double>(parm.nfz_screen.y()));

        // ── C → NFZ vector and unit direction ────────────────────────────────
        const double nfz_dx   = nfz_pt.x() - cx;
        const double nfz_dy   = nfz_pt.y() - cy;
        const double nfz_dist = std::sqrt(nfz_dx * nfz_dx + nfz_dy * nfz_dy);
        const double nfz_nx    = (nfz_dist > 1.0) ? nfz_dx / nfz_dist
                                                   : std::cos(fwd_angle);
        const double nfz_ny    = (nfz_dist > 1.0) ? nfz_dy / nfz_dist
                                                   : std::sin(fwd_angle);

        // ── Tangent contact points on the 50m circle toward NFZ ──────────────
        // Each convergent line is tangent to the circle; it originates at the
        // contact point and extends toward the NFZ.
        // Tangent angle α at C satisfies cos(α) = r / d  (screen space).
        const double phi = std::atan2(nfz_dy, nfz_dx);  // direction C → NFZ
        QPointF virt_left, virt_right;
        if (nfz_dist > r50_cbw + 0.5) {
            const double alpha = std::acos(std::min(1.0, r50_cbw / nfz_dist));
            virt_left  = QPointF(cx + r50_cbw * std::cos(phi + alpha),
                                 cy + r50_cbw * std::sin(phi + alpha));
            virt_right = QPointF(cx + r50_cbw * std::cos(phi - alpha),
                                 cy + r50_cbw * std::sin(phi - alpha));
        } else {
            // Degenerate: NFZ maps inside circle in screen space — use side points.
            virt_left  = QPointF(cx + r50_cbw * std::cos(fwd_angle + CBW_PI * 0.5),
                                 cy + r50_cbw * std::sin(fwd_angle + CBW_PI * 0.5));
            virt_right = QPointF(cx + r50_cbw * std::cos(fwd_angle - CBW_PI * 0.5),
                                 cy + r50_cbw * std::sin(fwd_angle - CBW_PI * 0.5));
        }

        // ── (b) Convergent lines: virtual endpoints → NFZ direction ──────────
        // Capped at 50 m visual equivalent; far NFZ gets a faded dotted tail
        // to show *tendency* without cluttering the map with a km-long line.
        const double cap_px = std::max(15.0, CBW_PHYS_PATH_M * pixels_per_meter);

        auto draw_conv = [&](const QPointF &src) {
            const double vdx = nfz_pt.x() - src.x();
            const double vdy = nfz_pt.y() - src.y();
            const double vd  = std::sqrt(vdx * vdx + vdy * vdy);
            if (vd < 1.0) return;
            const double vux = vdx / vd;
            const double vuy = vdy / vd;
            const double solid_len = std::min(vd, cap_px);
            const QPointF tip(src.x() + vux * solid_len,
                              src.y() + vuy * solid_len);
            p.setPen(QPen(QColor(255, 165, 0, 155), 1.5));
            p.drawLine(src, tip);
            // Faded dotted extension when the NFZ is far (beyond cap)
            if (vd > cap_px) {
                const double fade_len =
                    std::min(cap_px * 0.4, vd - solid_len);
                if (fade_len > 1.0) {
                    const QPointF fade_end(src.x() + vux * (solid_len + fade_len),
                                           src.y() + vuy * (solid_len + fade_len));
                    QPen fade_pen(QColor(255, 165, 0, 50), 1.0);
                    fade_pen.setStyle(Qt::DotLine);
                    p.setPen(fade_pen);
                    p.drawLine(tip, fade_end);
                }
            }
        };

        if (parm.nfz_in_valid_range) {
            // Keep geometric truth: line intersection must stay at the true
            // NFZ center, independent of zoom or panel clipping.
            const QPointF nfz_anchor = nfz_pt;

            // Two-segment tangent lines:
            // Seg 1 (solid):  NFZ anchor → tangent contact point on circle
            // Seg 2 (dashed): contact point → outward along same tangent direction,
            //                 length equals segment 1 (= distance NFZ anchor to contact).
            auto draw_tangent_two_seg = [&](const QPointF &contact_pt,
                                            const QPointF &nfz_anchor_pt) {
                const double dx = contact_pt.x() - nfz_anchor_pt.x();
                const double dy = contact_pt.y() - nfz_anchor_pt.y();
                const double d  = std::sqrt(dx * dx + dy * dy);
                if (d < 1e-6) return;
                const double ux = dx / d;
                const double uy = dy / d;
                // Seg 2 tip: same direction and same length beyond the contact point.
                const QPointF seg2_tip(contact_pt.x() + ux * d,
                                       contact_pt.y() + uy * d);

                // Seg 1: NFZ anchor → contact point (solid deep yellow)
                QPen p1(QColor(255, 210, 0, 255), 2.8);
                p1.setCosmetic(true);
                p.setPen(p1);
                p.drawLine(nfz_anchor_pt, contact_pt);

                // Seg 2: contact point → outward tip (dashed deep yellow)
                QPen p2(QColor(255, 210, 0, 220), 2.2);
                p2.setCosmetic(true);
                p2.setStyle(Qt::DashLine);
                p.setPen(p2);
                p.drawLine(contact_pt, seg2_tip);
            };
            draw_tangent_two_seg(virt_left,  nfz_anchor);
            draw_tangent_two_seg(virt_right, nfz_anchor);

            // Rear fan expansion boundary: keep outline only, no large fill.
            const double n_px = std::max(1.0, parm.nfz_dist_m * pixels_per_meter);
            const double rear_r = std::min(2500.0, std::max(20.0, 2.0 * n_px));
            const double rear_center_angle = std::atan2(nfz_ny, nfz_nx);
            QPainterPath rear_fan;
            rear_fan.moveTo(cx, cy);
            const int REAR_ARC_SEGS = 64;
            for (int i = 0; i <= REAR_ARC_SEGS; ++i) {
                const double a = (rear_center_angle - half_angle) +
                    (2.0 * half_angle) * static_cast<double>(i) /
                    static_cast<double>(REAR_ARC_SEGS);
                rear_fan.lineTo(cx + rear_r * std::cos(a), cy + rear_r * std::sin(a));
            }
            rear_fan.lineTo(cx, cy);
            p.setPen(QPen(QColor(255, 165, 0, 40), 1.0, Qt::DotLine));
            p.drawPath(rear_fan);

        } else {
            // Reverse-side NFZ exists but outside [16km, 32km]: soften geometry.
            draw_conv(virt_left);
            draw_conv(virt_right);
            p.setPen(QPen(QColor(255, 170, 80, 120), 1.0, Qt::DotLine));
            const double shrink = std::max(12.0, r * 1.8);
            p.drawLine(QPointF(cx, cy),
                       QPointF(cx + nfz_nx * shrink, cy + nfz_ny * shrink));
        }
    }

    // ── Center dot ───────────────────────────────────────────────────────────
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 80, 80, 230));
    p.drawEllipse(QPointF(cx, cy), 5.0, 5.0);

    // ── Labels ───────────────────────────────────────────────────────────────
    QFont lbl_font;
    lbl_font.setPixelSize(11);
    p.setFont(lbl_font);
    p.setPen(QColor(200, 240, 255, 210));

    // "R=10m" beside the circle (left of forward direction)
    if (r > 18.0) {
        const double la = fwd_angle + CBW_PI * 0.5;  // perpendicular-left
        p.drawText(QPointF(cx + (r + 6.0) * std::cos(la) - 4.0,
                           cy + (r + 6.0) * std::sin(la)), "R=10m");
    }

    // "Path=50m" alongside the forward guidance line (only when long enough)
    if (visual_path_px > 18.0) {
        // Offset the label perpendicular-left of the path line
        const double perp = fwd_angle - CBW_PI * 0.5;
        p.drawText(QPointF(arc_mid.x() + visual_path_px * 0.4 * std::cos(fwd_angle)
                               + 6.0 * std::cos(perp),
                           arc_mid.y() + visual_path_px * 0.4 * std::sin(fwd_angle)
                               + 6.0 * std::sin(perp)),
                   "Path=50m");
    }

    if (parm.draw_oor_badge && !parm.nfz_in_valid_range && parm.nearest_reverse_dist_m > 0.0) {
        p.restore();
        crossbow_draw_oor_badge(p, parm.panel, parm.nearest_reverse_dist_m);
        return;
    }

    p.restore();
}

// ──────────────────────────────────────────────────────────────────────────────
// crossbow_draw_oor_badge — purple screen-center OOR warning
// ──────────────────────────────────────────────────────────────────────────────
void crossbow_draw_oor_badge(QPainter &p, const QRect &panel,
                             double nearest_reverse_dist_m)
{
    if (nearest_reverse_dist_m <= 0.0 || panel.width() < 40 || panel.height() < 40)
        return;

    const double km = nearest_reverse_dist_m / 1000.0;
    const QString msg = QString::fromUtf8("\u26a0  NFZ OUT OF RANGE  ") +
                        QString("%1 km  (need >=16 km)").arg(km, 0, 'f', 1);

    QFont warn_font;
    warn_font.setPixelSize(17);
    warn_font.setBold(true);
    p.setFont(warn_font);
    QFontMetrics fm(warn_font);

    const int pad_x = 18;
    const int pad_y = 10;
    const int text_w = fm.horizontalAdvance(msg);
    const int text_h = fm.height();
    const QPoint ctr = panel.center();
    QRect badge(ctr.x() - (text_w + 2 * pad_x) / 2,
                ctr.y() - (text_h + 2 * pad_y) / 2,
                text_w + 2 * pad_x,
                text_h + 2 * pad_y);
    badge = badge.intersected(panel.adjusted(8, 8, -8, -8));

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(160, 32, 240, 240), 2));
    p.setBrush(QColor(30, 8, 50, 230));
    p.drawRoundedRect(badge, 10, 10);

    p.setPen(QColor(230, 200, 255, 250));
    p.drawText(badge, Qt::AlignCenter, msg);
    p.restore();
}

// ──────────────────────────────────────────────────────────────────────────────
// crossbow_draw_hint
// ──────────────────────────────────────────────────────────────────────────────
void crossbow_draw_hint(QPainter &p, const QRect &panel)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QString hint =
        QString::fromUtf8(" \xe5\xbd\x88\xe5\xbc\x93\xef\xbc\x9a"
                          "\xe9\xbb\x9e\xe6\x93\x8a\xe5\x9c\xb0\xe5\x9c\x96"
                          "\xe8\xa8\xad\xe5\xae\x9a\xe8\xb5\xb7\xe5\xa7\x8b\xe9\xbb\x9e ");

    QFont hint_font;
    hint_font.setPixelSize(12);
    p.setFont(hint_font);

    const QFontMetrics fm(hint_font);
    const int text_w = fm.horizontalAdvance(hint);
    const int badge_w = text_w + 24;
    const int badge_h = 26;
    const int badge_x = panel.x() + (panel.width()  - badge_w) / 2;
    const int badge_y = panel.y() +  panel.height() - 54;

    const QRect badge(badge_x, badge_y, badge_w, badge_h);
    p.fillRect(badge, QColor(0, 20, 40, 185));
    p.setPen(QColor(0, 229, 255, 150));
    p.drawRect(badge);
    p.setPen(QColor(200, 240, 255, 220));
    p.drawText(badge, Qt::AlignCenter, hint);

    p.restore();
}
