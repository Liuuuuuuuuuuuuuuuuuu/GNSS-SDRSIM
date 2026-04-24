#include "gui/control/panel/control_paint.h"
#include "gui/control/panel/control_paint_internal.h"

#include <algorithm>
#include <cmath>

double control_paint_clamp_double(double v, double lo, double hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

namespace {

double g_control_text_scale = 1.0;
double g_caption_text_scale = 1.0;
double g_switch_option_text_scale = 1.0;
double g_value_text_scale = 1.0;
int g_uniform_text_pt = 0;
int g_uniform_label_pt = 0;
const ControlSliderPartOverrides *g_slider_part_overrides = nullptr;
ControlLayoutElementId g_current_slider_element = CTRL_LAYOUT_ELEMENT_NONE;

} // namespace

int control_paint_scale_pt_with_factor(int pt, double factor) {
  const double s = control_paint_clamp_double(g_control_text_scale, 0.75, 1.50);
  const double f = control_paint_clamp_double(factor, 0.70, 1.60);
  return std::max(1, (int)std::lround((double)pt * s * f));
}

void control_paint_set_text_scale(double scale) {
  g_control_text_scale = control_paint_clamp_double(scale, 0.75, 1.50);
}

double control_paint_get_text_scale() {
  return g_control_text_scale;
}

void control_paint_set_uniform_text_point_size(int point_size) {
  g_uniform_text_pt = std::max(0, point_size);
}

void control_paint_set_uniform_label_point_size(int point_size) {
  g_uniform_label_pt = std::max(0, point_size);
}

void control_paint_set_detail_scales(double master_scale,
                                     double caption_scale,
                                     double switch_option_scale,
                                     double value_scale) {
  g_control_text_scale = control_paint_clamp_double(master_scale, 0.75, 1.50);
  g_caption_text_scale =
      control_paint_clamp_double(caption_scale, 0.70, 1.60);
  g_switch_option_text_scale =
      control_paint_clamp_double(switch_option_scale, 0.70, 1.60);
  g_value_text_scale = control_paint_clamp_double(value_scale, 0.70, 1.60);
}

int control_paint_clamp_int(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

int control_paint_fit_text_point_size(const QFont &base_font,
                                      const QString &text,
                                      int width,
                                      int height,
                                      int min_pt,
                                      int max_pt,
                                      bool bold,
                                      double local_scale) {
  if (text.isEmpty() || width <= 2 || height <= 2) {
    return min_pt;
  }
  int lo = std::max(1, control_paint_scale_pt_with_factor(min_pt, local_scale));
  int hi = std::max(lo, control_paint_scale_pt_with_factor(max_pt, local_scale));
  for (int pt = hi; pt >= lo; --pt) {
    QFont f = base_font;
    f.setBold(bold);
    f.setPointSize(pt);
    QFontMetrics fm(f);
    if (fm.height() <= height && fm.horizontalAdvance(text) <= width) {
      return pt;
    }
  }
  return lo;
}

void control_paint_paint_disabled_overlay(QPainter &p, const Rect &r, int radius) {
  if (r.w <= 2 || r.h <= 2)
    return;
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor(185, 193, 202, 90), 1));
  p.setBrush(QColor(120, 128, 138, 78));
  p.drawRoundedRect(
      QRectF(r.x + 0.5, r.y + 0.5, std::max(1, r.w - 1), std::max(1, r.h - 1)),
      radius, radius);
  p.setRenderHint(QPainter::Antialiasing, false);
}

double control_paint_caption_text_scale() {
  return g_caption_text_scale;
}

double control_paint_switch_option_text_scale() {
  return g_switch_option_text_scale;
}

double control_paint_value_text_scale() {
  return g_value_text_scale;
}

int control_paint_uniform_text_pt() {
  return g_uniform_text_pt;
}

int control_paint_uniform_label_pt() {
  return g_uniform_label_pt;
}

void control_paint_set_slider_part_overrides(const ControlSliderPartOverrides *overrides) {
  g_slider_part_overrides = overrides;
}

void control_paint_set_current_slider_element(ControlLayoutElementId id) {
  g_current_slider_element = id;
}

static void apply_slider_part_adjustment(QRect *rect,
                                         const ControlRectAdjustment *adj) {
  if (!rect || !adj || rect->width() <= 0 || rect->height() <= 0) {
    return;
  }
  rect->translate(adj->dx, adj->dy);
  rect->setWidth(std::max(12, rect->width() + adj->dw));
  rect->setHeight(std::max(10, rect->height() + adj->dh));
}

static const ControlRectAdjustment *slider_part_adj(
    const ControlRectAdjustment parts[CTRL_LAYOUT_ELEMENT_COUNT]) {
  if (!g_slider_part_overrides || !parts ||
      g_current_slider_element <= CTRL_LAYOUT_ELEMENT_NONE ||
      g_current_slider_element >= CTRL_LAYOUT_ELEMENT_COUNT) {
    return nullptr;
  }
  return &parts[g_current_slider_element];
}

void control_paint_apply_slider_label_adjustment(QRect *rect) {
  const ControlRectAdjustment *adj =
      slider_part_adj(g_slider_part_overrides ? g_slider_part_overrides->label
                                              : nullptr);
  apply_slider_part_adjustment(rect, adj);
}

void control_paint_apply_slider_track_adjustment(QRect *rect) {
  const ControlRectAdjustment *adj =
      slider_part_adj(g_slider_part_overrides ? g_slider_part_overrides->track
                                              : nullptr);
  apply_slider_part_adjustment(rect, adj);
}

void control_paint_apply_slider_value_adjustment(QRect *rect) {
  const ControlRectAdjustment *adj =
      slider_part_adj(g_slider_part_overrides ? g_slider_part_overrides->value
                                              : nullptr);
  apply_slider_part_adjustment(rect, adj);
}