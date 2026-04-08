#ifndef CONTROL_PAINT_H
#define CONTROL_PAINT_H

#include <QColor>
#include <QPainter>

#include "gui/layout/control_layout.h"

void control_paint_set_text_scale(double scale);
double control_paint_get_text_scale();
void control_paint_set_detail_scales(double master_scale,
                                     double caption_scale,
                                     double switch_option_scale,
                                     double value_scale);
void control_paint_set_uniform_text_point_size(int point_size);
void control_paint_set_uniform_label_point_size(int point_size);

void control_draw_button(QPainter &p, const Rect &r, const QColor &border, const QColor &text, const char *label);
void control_draw_button_filled(QPainter &p,
                                const Rect &r,
                                const QColor &fill,
                                const QColor &border,
                                const QColor &text,
                                const char *label);
void control_draw_checkbox(QPainter &p,
                           const Rect &r,
                           const QColor &border,
                           const QColor &text,
                           const QColor &dim,
                           const char *label,
                           bool on,
                           bool enabled);
void control_draw_slider(QPainter &p,
                         const Rect &r,
                         const QColor &border,
                         const QColor &text,
                         const QColor &dim,
                         const QColor &accent,
                         const char *name,
                         const char *value,
                         double ratio,
                         bool enabled);
            void control_draw_slider_stacked(QPainter &p,
                             const Rect &r,
                             const QColor &border,
                             const QColor &text,
                             const QColor &dim,
                             const QColor &accent,
                             const char *name,
                             const char *value,
                             double ratio,
                             bool enabled,
                             bool show_fs_ticks,
                             bool emphasize_caption);
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
                               bool enabled);
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
                             bool enabled);
void control_draw_text_right(QPainter &p, int right_x, int baseline_y, const char *text);

#endif
