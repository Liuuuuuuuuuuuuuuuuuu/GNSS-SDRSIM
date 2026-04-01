#include "gui/control_paint.h"

#include <algorithm>
#include <cmath>

static inline double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void control_draw_button(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const char *label)
{
    QRect rr(r.x, r.y, r.w - 1, r.h - 1);
    QColor fill(18, 28, 45, 210);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(rr, 4, 4);
    p.setPen(text);
    p.drawText(rr, Qt::AlignCenter, label);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_button_filled(QPainter &p,
                                const Rect &r,
                                const QColor &fill,
                                const QColor &border,
                                const QColor &text,
                                const char *label)
{
    QRect rr(r.x, r.y, r.w - 1, r.h - 1);
    QColor top = fill.lighter(118);
    QColor bot = fill.darker(118);
    QLinearGradient grad(rr.topLeft(), rr.bottomLeft());
    grad.setColorAt(0.0, top);
    grad.setColorAt(1.0, bot);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1));
    p.setBrush(grad);
    p.drawRoundedRect(rr, 4, 4);
    p.setPen(text);
    p.drawText(rr, Qt::AlignCenter, label);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_checkbox(QPainter &p,
                           const Rect &r,
                           const QColor &border,
                           const QColor &text,
                           const QColor &dim,
                           const char *label,
                           bool on,
                           bool enabled)
{
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QRect box(r.x, r.y + (r.h - 12) / 2, 12, 12);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(15, 25, 39, 210));
    p.drawRoundedRect(box, 2, 2);
    if (on) {
        p.setPen(QPen(QColor("#4ade80"), 2));
        p.drawLine(box.x() + 2, box.y() + 6, box.x() + 5, box.y() + 9);
        p.drawLine(box.x() + 5, box.y() + 9, box.x() + 10, box.y() + 3);
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setPen(t);
    p.drawText(r.x + 17, r.y + r.h - 3, label);
}

void control_draw_slider(QPainter &p,
                         const Rect &r,
                         const QColor &border,
                         const QColor &text,
                         const QColor &dim,
                         const QColor &accent,
                         const char *name,
                         const char *value,
                         double ratio,
                         bool enabled)
{
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor fill = enabled ? accent : QColor(71, 85, 105, 150);
    ratio = clamp_double(ratio, 0.0, 1.0);
    Rect vrect = slider_value_rect(r);
    int track_end = vrect.x - 8;
    if (track_end < r.x + 20) track_end = r.x + 20;
    int track_w = track_end - r.x;

    p.setPen(t);
    p.drawText(r.x, r.y - 1, name);
    QRect vb(vrect.x, vrect.y, vrect.w - 1, vrect.h - 1);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(15, 25, 39, 220));
    p.drawRoundedRect(vb, 3, 3);
    p.setPen(t);
    p.drawText(vb, Qt::AlignCenter, value);
    p.setRenderHint(QPainter::Antialiasing, false);

    int ty = r.y + r.h / 2 - 2;
    QRect track(r.x, ty, track_w - 1, 4);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(15, 25, 39, 220));
    p.drawRoundedRect(track, 2, 2);

    int fill_w = (int)llround((double)(track_w - 1) * ratio);
    if (fill_w > 2) {
        QRect filled(r.x, ty, fill_w, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(filled, 2, 2);
    }

    int knob_x = r.x + (int)llround((double)(track_w - 1) * ratio);
    int knob_y = r.y + r.h / 2;
    p.setPen(QPen(edge, 1));
    p.setBrush(enabled ? QColor("#e2e8f0") : QColor("#94a3b8"));
    p.drawEllipse(QPoint(knob_x, knob_y), 5, 5);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_three_switch(QPainter &p,
                               const Rect &r,
                               const QColor &border,
                               const QColor &text,
                               const QColor &dim,
                               const QColor &active_fill,
                               const char *caption,
                               const char *a,
                               const char *b,
                               const char *c,
                               int active_idx,
                               bool enabled)
{
    const char *labels[3] = {a, b, c};
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);

    p.setPen(t);
    p.drawText(r.x, r.y - 1, caption);

    QRect outer(r.x, r.y, r.w - 1, r.h - 1);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(16, 26, 40, 190));
    p.drawRoundedRect(outer, 4, 4);

    int seg_w = r.w / 3;
    for (int i = 0; i < 3; ++i) {
        int sx = r.x + i * seg_w;
        int sw = (i == 2) ? (r.w - 2 * seg_w) : seg_w;
        QRect seg(sx, r.y, sw - 1, r.h - 1);

        if (i == active_idx) {
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRoundedRect(seg.adjusted(1, 1, -1, -1), 3, 3);
        }

        if (i > 0) {
            p.setPen(QPen(edge, 1));
            p.drawLine(sx, r.y + 2, sx, r.y + r.h - 3);
        }

        p.setPen(t);
        p.drawText(seg, Qt::AlignCenter, labels[i]);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_two_switch(QPainter &p,
                             const Rect &r,
                             const QColor &border,
                             const QColor &text,
                             const QColor &dim,
                             const QColor &active_fill,
                             const char *caption,
                             const char *a,
                             const char *b,
                             int active_idx,
                             bool enabled)
{
    const char *labels[2] = {a, b};
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);

    p.setPen(t);
    p.drawText(r.x, r.y - 1, caption);

    QRect outer(r.x, r.y, r.w - 1, r.h - 1);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(16, 26, 40, 190));
    p.drawRoundedRect(outer, 4, 4);

    int seg_w = r.w / 2;
    for (int i = 0; i < 2; ++i) {
        int sx = r.x + i * seg_w;
        int sw = (i == 1) ? (r.w - seg_w) : seg_w;
        QRect seg(sx, r.y, sw - 1, r.h - 1);

        if (i == active_idx) {
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRoundedRect(seg.adjusted(1, 1, -1, -1), 3, 3);
        }

        if (i > 0) {
            p.setPen(QPen(edge, 1));
            p.drawLine(sx, r.y + 2, sx, r.y + r.h - 3);
        }

        p.setPen(t);
        p.drawText(seg, Qt::AlignCenter, labels[i]);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_text_right(QPainter &p, int right_x, int baseline_y, const char *text)
{
    if (!text) return;
    int w = p.fontMetrics().horizontalAdvance(text);
    p.drawText(right_x - w, baseline_y, text);
}
