#include "gui/control/control_paint.h"
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <cstring>

static inline double clamp_double(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void control_draw_button(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const char *label) {
    QFont old_font = p.font();
    QFont f = old_font;
    f.setPointSize(clamp_int(std::max(9, (int)std::round(r.h * 0.40)), 9, 15));
    p.setFont(f);
    QPainterPath path;
    int cl = 6;
    path.moveTo(r.x + cl, r.y);
    path.lineTo(r.x + r.w, r.y);
    path.lineTo(r.x + r.w, r.y + r.h - cl);
    path.lineTo(r.x + r.w - cl, r.y + r.h);
    path.lineTo(r.x, r.y + r.h);
    path.lineTo(r.x, r.y + cl);
    path.closeSubpath();

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1));
    p.setBrush(QColor(18, 28, 45, 210));
    p.drawPath(path);
    p.setPen(text);
    p.drawText(QRect(r.x, r.y, r.w, r.h), Qt::AlignCenter, label);
    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_button_filled(QPainter &p, const Rect &r, const QColor &fill, const QColor &border, const QColor &text, const char *label) {
    QFont old_font = p.font();
    QPainterPath path;
    int cl = 10; 
    path.moveTo(r.x + cl, r.y);
    path.lineTo(r.x + r.w, r.y);
    path.lineTo(r.x + r.w, r.y + r.h - cl);
    path.lineTo(r.x + r.w - cl, r.y + r.h);
    path.lineTo(r.x, r.y + r.h);
    path.lineTo(r.x, r.y + cl);
    path.closeSubpath();

    QLinearGradient grad(r.x, r.y, r.x, r.y + r.h);
    grad.setColorAt(0.0, fill.lighter(115));
    grad.setColorAt(1.0, fill.darker(115));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1));
    p.setBrush(grad);
    p.drawPath(path);
    
    p.setPen(text);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(clamp_int(std::max(9, (int)std::round(r.h * 0.42)), 9, 16));
    f.setLetterSpacing(QFont::PercentageSpacing, 110);
    p.setFont(f);
    p.drawText(QRect(r.x, r.y, r.w, r.h), Qt::AlignCenter, label);
    f.setBold(false);
    f = old_font;
    f.setLetterSpacing(QFont::PercentageSpacing, 100);
    p.setFont(f);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void control_draw_checkbox(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const QColor &dim, const char *label, bool on, bool enabled) {
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    int box_size = clamp_int((int)std::round((double)r.h * 0.62), 14, 20);
    QRect box(r.x, r.y + (r.h - box_size) / 2, box_size, box_size);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(10, 20, 30, 200));
    p.drawRect(box);
    
    if (on) {
        p.setPen(QPen(QColor("#00ffcc"), 3)); 
        p.drawLine(box.x() + 4, box.y() + 9, box.x() + 8, box.y() + 13);
        p.drawLine(box.x() + 8, box.y() + 13, box.x() + 14, box.y() + 4);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(t);
    QFont old_font = p.font();
    QFont f = old_font;
    f.setPointSize(clamp_int(std::max(8, (int)std::round(r.h * 0.38)), 8, 14));
    p.setFont(f);
    p.drawText(QRect(r.x + box_size + 10, r.y, std::max(0, r.w - box_size - 10), r.h),
               Qt::AlignVCenter | Qt::AlignLeft, label);
    p.setFont(old_font);
}

void control_draw_slider(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const QColor &dim, const QColor &accent, const char *name, const char *value, double ratio, bool enabled) {
    QFont old_font = p.font();
    QFont small_font = old_font;
    small_font.setPointSize(clamp_int(std::max(9, (int)std::round(r.h * 0.38)), 9, 15));
    p.setFont(small_font);
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor f_color = enabled ? accent : QColor(71, 85, 105, 150);
    ratio = clamp_double(ratio, 0.0, 1.0);
    
    Rect vrect = slider_value_rect(r);
    int label_w = clamp_int((int)std::lround((double)r.w * 0.34), 86, 190);
    int label_x = r.x;
    int min_track_x = label_x + label_w + 8;
    if (vrect.x <= min_track_x + 20) {
        label_w = std::max(52, vrect.x - r.x - 28);
        min_track_x = label_x + label_w + 8;
    }

    QRect label_rect(label_x, r.y, std::max(24, label_w), r.h);
    int track_start = std::min(std::max(min_track_x, r.x + 8), vrect.x - 20);
    int track_end = vrect.x - 10;
    int track_w = std::max(10, track_end - track_start);

    // 名稱改為同一列左側，避免上下列標題互相重疊
    p.setPen(t);
    p.drawText(label_rect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(name, Qt::ElideRight, label_rect.width()));

    // 繪製輸入框 (Value Box)
    QPainterPath vpath;
    int cl = 4;
    vpath.moveTo(vrect.x + cl, vrect.y);
    vpath.lineTo(vrect.x + vrect.w, vrect.y);
    vpath.lineTo(vrect.x + vrect.w, vrect.y + vrect.h - cl);
    vpath.lineTo(vrect.x + vrect.w - cl, vrect.y + vrect.h);
    vpath.lineTo(vrect.x, vrect.y + vrect.h);
    vpath.lineTo(vrect.x, vrect.y + cl);
    vpath.closeSubpath();

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(10, 20, 35, 220));
    p.drawPath(vpath);
    p.setPen(enabled ? QColor("#00ffcc") : dim);
    
    QFont fb = p.font(); fb.setBold(true); p.setFont(fb);
    p.drawText(QRect(vrect.x, vrect.y, vrect.w, vrect.h), Qt::AlignCenter, value);
    fb.setBold(false); p.setFont(fb);

    int track_thick = (r.h > 24) ? 8 : 6;
    int ty = r.y + r.h / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 45, 60, 180));
    p.drawRect(track_start, ty - track_thick/2, track_w, track_thick);

    int fill_w = (int)llround((double)track_w * ratio);
    if (fill_w > 0) {
        p.setBrush(f_color);
        p.drawRect(track_start, ty - track_thick/2, fill_w, track_thick);
    }

    // === FS 專屬刻度繪製 ===
    if (std::strcmp(name, "FS (Frequency)") == 0) {
        QFont orig_font = p.font();
        auto draw_tick = [&](double val, const char* lbl) {
            double tick_ratio = (val - 2.6) / std::max(0.1, 31.2 - 2.6);
            int tx = track_start + (int)llround(track_w * tick_ratio);
            
            p.setPen(QPen(QColor(139, 195, 255, 200), 2)); 
            p.drawLine(tx, ty - track_thick/2 - 3, tx, ty + track_thick/2 + 3);
            
            QFont f = orig_font;
            f.setPointSize(f.pointSize() - 1);
            p.setFont(f);
            p.setPen(QColor(139, 195, 255, 255));
            p.drawText(tx - 8, ty + track_thick/2 + 15, lbl);
        };
        p.setRenderHint(QPainter::Antialiasing, false);
        draw_tick(2.6, "2.6");
        draw_tick(5.2, "5.2");
        draw_tick(20.8, "20.8");
        p.setFont(orig_font); 
    }

    // 發光菱形旋鈕
    int knob_size = (r.h > 24) ? 10 : 7;
    int knob_x = track_start + fill_w;
    QPainterPath knob;
    knob.moveTo(knob_x, ty - knob_size);
    knob.lineTo(knob_x + knob_size, ty);
    knob.lineTo(knob_x, ty + knob_size);
    knob.lineTo(knob_x - knob_size, ty);
    knob.closeSubpath();
    
    p.setPen(QPen(edge, 1));
    p.setBrush(enabled ? QColor("#ffffff") : dim);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPath(knob);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(old_font);
}

void control_draw_three_switch(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const QColor &dim, const QColor &active_fill, const char *caption, const char *a, const char *b, const char *c, int active_idx, bool enabled) {
    const char *labels[3] = {a, b, c};
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);

    QFont old_font = p.font();
    QFont fcap = old_font;
    fcap.setPointSize(clamp_int(std::max(8, (int)std::round(r.h * 0.26)), 8, 13));
    p.setFont(fcap);

    const int caption_h = clamp_int((int)std::lround((double)r.h * 0.34), 11, std::max(11, r.h - 10));
    const int seg_y = r.y + caption_h;
    const int seg_h = std::max(8, r.h - caption_h);

    p.setPen(t);
    p.drawText(QRect(r.x + 6, r.y + 1, std::max(12, r.w - 12), std::max(10, caption_h - 2)),
               Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(caption, Qt::ElideRight, std::max(12, r.w - 12)));

    QPainterPath path;
    int cl = 8;
    path.moveTo(r.x + cl, seg_y);
    path.lineTo(r.x + r.w - cl, seg_y);
    path.lineTo(r.x + r.w, seg_y + cl);
    path.lineTo(r.x + r.w, seg_y + seg_h - cl);
    path.lineTo(r.x + r.w - cl, seg_y + seg_h);
    path.lineTo(r.x + cl, seg_y + seg_h);
    path.lineTo(r.x, seg_y + seg_h - cl);
    path.lineTo(r.x, seg_y + cl);
    path.closeSubpath();

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(10, 20, 30, 200));
    p.drawPath(path);

    int seg_w = r.w / 3;
    for (int i = 0; i < 3; ++i) {
        int sx = r.x + i * seg_w;
        int sw = (i == 2) ? (r.w - 2 * seg_w) : seg_w;
        QRect seg(sx, seg_y, sw, seg_h);

        if (i == active_idx) {
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRect(sx + 4, seg_y + seg_h - 6, sw - 8, 4); 
            p.setBrush(QColor(active.red(), active.green(), active.blue(), 60));
            p.drawRect(sx + 1, seg_y + 1, sw - 2, seg_h - 2);
        }
        if (i > 0) {
            p.setPen(QPen(QColor(edge.red(), edge.green(), edge.blue(), 100), 1));
            p.drawLine(sx, seg_y + 6, sx, seg_y + seg_h - 6);
        }
        p.setPen(i == active_idx ? QColor("#ffffff") : t);
        QFont f = p.font(); f.setBold(i == active_idx); p.setFont(f);
        p.drawText(seg, Qt::AlignCenter,
                   p.fontMetrics().elidedText(labels[i], Qt::ElideRight, std::max(10, sw - 8)));
        f.setBold(false); p.setFont(f);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(old_font);
}

void control_draw_two_switch(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const QColor &dim, const QColor &active_fill, const char *caption, const char *a, const char *b, int active_idx, bool enabled) {
    const char *labels[2] = {a, b};
    QColor edge = enabled ? border : dim;
    QColor t = enabled ? text : dim;
    QColor active = enabled ? active_fill : QColor(60, 76, 96, 190);
    bool neutral = active_idx < 0;

    QFont old_font = p.font();
    QFont fcap = old_font;
    fcap.setPointSize(clamp_int(std::max(8, (int)std::round(r.h * 0.26)), 8, 13));
    p.setFont(fcap);

    const int caption_h = clamp_int((int)std::lround((double)r.h * 0.34), 11, std::max(11, r.h - 10));
    const int seg_y = r.y + caption_h;
    const int seg_h = std::max(8, r.h - caption_h);

    p.setPen(t);
    p.drawText(QRect(r.x + 6, r.y + 1, std::max(12, r.w - 12), std::max(10, caption_h - 2)),
               Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(caption, Qt::ElideRight, std::max(12, r.w - 12)));

    QPainterPath path;
    int cl = 8;
    path.moveTo(r.x + cl, seg_y);
    path.lineTo(r.x + r.w - cl, seg_y);
    path.lineTo(r.x + r.w, seg_y + cl);
    path.lineTo(r.x + r.w, seg_y + seg_h - cl);
    path.lineTo(r.x + r.w - cl, seg_y + seg_h);
    path.lineTo(r.x + cl, seg_y + seg_h);
    path.lineTo(r.x, seg_y + seg_h - cl);
    path.lineTo(r.x, seg_y + cl);
    path.closeSubpath();

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(edge, 1));
    p.setBrush(QColor(10, 20, 30, 200));
    p.drawPath(path);

    int seg_w = r.w / 2;
    for (int i = 0; i < 2; ++i) {
        int sx = r.x + i * seg_w;
        int sw = (i == 1) ? (r.w - seg_w) : seg_w;
        QRect seg(sx, seg_y, sw, seg_h);

        if (!neutral && i == active_idx) {
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRect(sx + 4, seg_y + seg_h - 6, sw - 8, 4);
            p.setBrush(QColor(active.red(), active.green(), active.blue(), 60));
            p.drawRect(sx + 1, seg_y + 1, sw - 2, seg_h - 2);
        }
        if (i > 0) {
            p.setPen(QPen(QColor(edge.red(), edge.green(), edge.blue(), 100), 1));
            p.drawLine(sx, seg_y + 6, sx, seg_y + seg_h - 6);
        }
        p.setPen(!neutral && i == active_idx ? QColor("#ffffff") : t);
        QFont f = p.font(); f.setBold(!neutral && i == active_idx); p.setFont(f);
        p.drawText(seg, Qt::AlignCenter,
                   p.fontMetrics().elidedText(labels[i], Qt::ElideRight, std::max(10, sw - 8)));
        f.setBold(false); p.setFont(f);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(old_font);
}

void control_draw_text_right(QPainter &p, int right_x, int baseline_y, const char *text) {
    if (!text) return;
    int w = p.fontMetrics().horizontalAdvance(text);
    p.drawText(right_x - w, baseline_y, text);
}