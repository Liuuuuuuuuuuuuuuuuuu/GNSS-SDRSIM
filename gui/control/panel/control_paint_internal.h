#ifndef CONTROL_PAINT_INTERNAL_H
#define CONTROL_PAINT_INTERNAL_H

#include <QFont>
#include <QPainter>
#include <QString>

#include "gui/layout/geometry/control_layout.h"

double control_paint_clamp_double(double v, double lo, double hi);
int control_paint_clamp_int(int v, int lo, int hi);
int control_paint_scale_pt_with_factor(int pt, double factor);
int control_paint_fit_text_point_size(const QFont &base_font,
                                      const QString &text,
                                      int width,
                                      int height,
                                      int min_pt,
                                      int max_pt,
                                      bool bold = false,
                                      double local_scale = 1.0);
void control_paint_paint_disabled_overlay(QPainter &p,
                                          const Rect &r,
                                          int radius = 7);

double control_paint_caption_text_scale();
double control_paint_switch_option_text_scale();
double control_paint_value_text_scale();
int control_paint_uniform_text_pt();
int control_paint_uniform_label_pt();

#endif
