#include "gui/layout/map_monitor_panels_utils.h"

#include "gui/layout/monitor_panel_utils.h"
#include "gui/layout/quad_panel_layout.h"
#include "gui/core/gui_i18n.h"
#include "gui/core/rf_mode_utils.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "bdssim.h"
}

namespace {

void draw_panel_frame(QPainter &p, const QRect &panel) {
  QLinearGradient panel_grad(panel.topLeft(), panel.bottomLeft());
  panel_grad.setColorAt(0.0, QColor("#101f33"));
  panel_grad.setColorAt(1.0, QColor("#081423"));
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(panel_grad);
  p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setPen(QPen(QColor("#c4d2e4"), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
}

QRect draw_panel_base(QPainter &p, int panel_x, int panel_y, int panel_w,
                      int panel_h, int x_div, int y_div) {
  QRect panel(panel_x, panel_y, panel_w, panel_h);
  draw_panel_frame(p, panel);
  QRect draw_rect = monitor_plot_rect(panel_x, panel_y, panel_w, panel_h);
  if (draw_rect.width() >= 8 && draw_rect.height() >= 8) {
    draw_monitor_inner_grid(p, draw_rect, QColor("#c4d2e4"), QColor(196, 210, 228, 56),
                            x_div, y_div);
  }
  return draw_rect;
}

void draw_panel_title(QPainter &p, int panel_x, int panel_y, int panel_h,
                      const char *title) {
  QFont old_font = p.font();
  QFont title_font = old_font;
  title_font.setBold(true);
  title_font.setPointSize(std::max(12, std::min(18, panel_h / 18)));
  p.setFont(title_font);
  p.setPen(QColor("#8fc7ff"));
  p.drawText(panel_x + 10, panel_y + 24, title);
  p.setFont(old_font);
}

} // namespace

void map_draw_spectrum_panel(QPainter &p, int win_width, int win_height,
                             const MapMonitorPanelsInput &in) {
  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w,
                       &panel_h, false);
  if (panel_w < 180 || panel_h < 120)
    return;

  QRect draw_rect = draw_panel_base(p, panel_x, panel_y, panel_w, panel_h, 6, 4);
  if (draw_rect.width() < 8 || draw_rect.height() < 8)
    return;

  if (in.spec_snap.valid && in.spec_snap.bins >= 8) {
    p.setPen(QPen(QColor("#51a7ff"), 1));
    QPainterPath path;
    for (int i = 0; i < in.spec_snap.bins; ++i) {
      float t = (float)i / (float)(in.spec_snap.bins - 1);
      int x = draw_rect.left() + (int)llround(t * (float)(draw_rect.width() - 1));
      float norm = (in.spec_snap.rel_db[i] + 30.0f) / 30.0f;
      if (norm < 0.0f) norm = 0.0f;
      if (norm > 1.0f) norm = 1.0f;
      int y = draw_rect.bottom() - (int)llround(norm * (float)(draw_rect.height() - 1));
      if (i == 0) path.moveTo(x, y);
      else path.lineTo(x, y);
    }
    p.setBrush(Qt::NoBrush);
    p.save();
    p.setClipRect(draw_rect);
    p.drawPath(path);
    p.setPen(QPen(QColor(81, 167, 255, 96), 3));
    p.drawPath(path);
    p.restore();
  }

  const QByteArray spectrum_title =
      gui_i18n_text(in.language, "monitor.spectrum").toUtf8();
  draw_panel_title(p, panel_x, panel_y, panel_h, spectrum_title.constData());

  const int db_max = 0;
  const int db_mid = -15;
  const int db_min = -30;
  char db_top[24], db_mid_s[24], db_bot[24];
  std::snprintf(db_top, sizeof(db_top), "%d dB", db_max);
  std::snprintf(db_mid_s, sizeof(db_mid_s), "%d dB", db_mid);
  std::snprintf(db_bot, sizeof(db_bot), "%d dB", db_min);
  p.setPen(QColor("#c4d2e4"));
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top()), Qt::AlignRight | Qt::AlignVCenter, db_top);
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top() + draw_rect.height() / 2), Qt::AlignRight | Qt::AlignVCenter, db_mid_s);
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.bottom()), Qt::AlignRight | Qt::AlignVCenter, db_bot);

  const double cf_mhz = mode_tx_center_hz(in.ctrl.signal_mode) / 1e6;
  const double fs_mhz = (in.ctrl.fs_mhz > 0.0) ? in.ctrl.fs_mhz : (mode_default_fs_hz(in.ctrl.signal_mode) / 1e6);
  const double half_bw_mhz = fs_mhz * 0.5;
  char lbl_l[24], lbl_c[24], lbl_r[24];
  std::snprintf(lbl_l, sizeof(lbl_l), "%.3f MHz", cf_mhz - half_bw_mhz);
  std::snprintf(lbl_c, sizeof(lbl_c), "%.3f MHz", cf_mhz);
  std::snprintf(lbl_r, sizeof(lbl_r), "%.3f MHz", cf_mhz + half_bw_mhz);
  draw_monitor_x_labels(p, draw_rect, draw_rect.bottom() + 8, lbl_l, lbl_c, lbl_r);
}

void map_draw_waterfall_panel(QPainter &p, int win_width, int win_height,
                              const MapMonitorPanelsInput &in) {
  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w,
                       &panel_h, true);
  if (panel_w < 180 || panel_h < 120)
    return;

  QRect draw_rect = draw_panel_base(p, panel_x, panel_y, panel_w, panel_h, 6, 4);
  if (draw_rect.width() < 8 || draw_rect.height() < 8)
    return;

  QLinearGradient plot_grad(draw_rect.topLeft(), draw_rect.bottomLeft());
  plot_grad.setColorAt(0.0, QColor("#101f33"));
  plot_grad.setColorAt(1.0, QColor("#081423"));
  p.fillRect(draw_rect, plot_grad);
  if (!in.waterfall_image.isNull()) {
    p.drawImage(draw_rect, in.waterfall_image);
  }
  draw_monitor_inner_grid(p, draw_rect, QColor("#c4d2e4"), QColor(196, 210, 228, 56), 6, 4);
  const QByteArray waterfall_title =
      gui_i18n_text(in.language, "monitor.waterfall").toUtf8();
  draw_panel_title(p, panel_x, panel_y, panel_h, waterfall_title.constData());

  p.setPen(QColor("#c4d2e4"));
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top()), Qt::AlignRight | Qt::AlignVCenter, "0s");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.center().y()), Qt::AlignRight | Qt::AlignVCenter, "5s");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.bottom()), Qt::AlignRight | Qt::AlignVCenter, "10s");

  const double cf_mhz = mode_tx_center_hz(in.ctrl.signal_mode) / 1e6;
  const double fs_mhz = (in.ctrl.fs_mhz > 0.0) ? in.ctrl.fs_mhz : (mode_default_fs_hz(in.ctrl.signal_mode) / 1e6);
  const double half_bw_mhz = fs_mhz * 0.5;
  char lbl_l[24], lbl_c[24], lbl_r[24];
  std::snprintf(lbl_l, sizeof(lbl_l), "%.3f MHz", cf_mhz - half_bw_mhz);
  std::snprintf(lbl_c, sizeof(lbl_c), "%.3f MHz", cf_mhz);
  std::snprintf(lbl_r, sizeof(lbl_r), "%.3f MHz", cf_mhz + half_bw_mhz);
  draw_monitor_x_labels(p, draw_rect, draw_rect.bottom() + 8, lbl_l, lbl_c, lbl_r);
}

void map_draw_time_panel(QPainter &p, int win_width, int win_height,
                         const MapMonitorPanelsInput &in) {
  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w,
                       &panel_h, false);
  if (panel_w < 180 || panel_h < 120)
    return;

  QRect draw_rect = draw_panel_base(p, panel_x, panel_y, panel_w, panel_h, 6, 4);
  if (draw_rect.width() < 8 || draw_rect.height() < 8)
    return;

  float y_scale = (float)(draw_rect.height() - 1) * 0.5f;
  float y_mid = (float)draw_rect.center().y();
  if (in.spec_snap.time_valid && in.spec_snap.time_samples >= 8 &&
      draw_rect.width() > 2 && draw_rect.height() > 2) {
    QPainterPath path_q;
    QPainterPath path_i;
    for (int i = 0; i < in.spec_snap.time_samples; ++i) {
      float t = (float)i / (float)(in.spec_snap.time_samples - 1);
      int x = draw_rect.left() + (int)llround(t * (float)(draw_rect.width() - 1));
      int yq = (int)llround(y_mid - in.spec_snap.time_q[i] * y_scale);
      int yi = (int)llround(y_mid - in.spec_snap.time_i[i] * y_scale);
      if (i == 0) {
        path_q.moveTo(x, yq);
        path_i.moveTo(x, yi);
      } else {
        path_q.lineTo(x, yq);
        path_i.lineTo(x, yi);
      }
    }
    p.setPen(QPen(QColor("#51a7ff"), 1));
    p.setBrush(Qt::NoBrush);
    p.save();
    p.setClipRect(draw_rect);
    p.drawPath(path_q);
    p.setPen(QPen(QColor("#ef4444"), 1));
    p.drawPath(path_i);
    p.setPen(QPen(QColor(81, 167, 255, 92), 2));
    p.drawPath(path_q);
    p.setPen(QPen(QColor(239, 68, 68, 92), 2));
    p.drawPath(path_i);
    p.restore();
  }

  const QByteArray time_title =
      gui_i18n_text(in.language, "monitor.time").toUtf8();
  draw_panel_title(p, panel_x, panel_y, panel_h, time_title.constData());
  p.setPen(QColor("#c4d2e4"));
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top()), Qt::AlignRight | Qt::AlignVCenter, "+1.0");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top() + draw_rect.height() / 4), Qt::AlignRight | Qt::AlignVCenter, "+0.5");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.center().y()), Qt::AlignRight | Qt::AlignVCenter, "0.0");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top() + (draw_rect.height() * 3) / 4), Qt::AlignRight | Qt::AlignVCenter, "-0.5");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.bottom()), Qt::AlignRight | Qt::AlignVCenter, "-1.0");

  const double fs_hz = (in.ctrl.fs_mhz > 0.0) ? (in.ctrl.fs_mhz * 1e6) : mode_default_fs_hz(in.ctrl.signal_mode);
  const double time_span_ms = ((double)in.spec_snap.time_samples / fs_hz) * 1000.0;
  char t_l[24], t_c[24], t_r[24];
  std::snprintf(t_l, sizeof(t_l), "0.00ms");
  std::snprintf(t_c, sizeof(t_c), "%.2fms", time_span_ms * 0.5);
  std::snprintf(t_r, sizeof(t_r), "%.2fms", time_span_ms);
  draw_monitor_x_labels(p, draw_rect, draw_rect.bottom() + 8, t_l, t_c, t_r);
}

void map_draw_constellation_panel(QPainter &p, int win_width, int win_height,
                                   const MapMonitorPanelsInput &in) {
  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w,
                       &panel_h, true);
  if (panel_w < 180 || panel_h < 120)
    return;

  QRect draw_rect = draw_panel_base(p, panel_x, panel_y, panel_w, panel_h, 4, 4);
  if (draw_rect.width() < 16 || draw_rect.height() < 16)
    return;

  int cx = draw_rect.center().x();
  int cy = draw_rect.center().y();
  p.setPen(QPen(QColor("#c4d2e4"), 1));
  p.drawLine(draw_rect.left(), cy, draw_rect.right(), cy);
  p.drawLine(cx, draw_rect.top(), cx, draw_rect.bottom());

  if (in.spec_snap.time_valid && in.spec_snap.time_samples >= 8) {
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#51a7ff"));
    int step = in.spec_snap.time_samples / 256;
    if (step < 1) step = 1;
    for (int i = 0; i < in.spec_snap.time_samples; i += step) {
      float ii = std::max(-1.0f, std::min(1.0f, in.spec_snap.time_i[i]));
      float qq = std::max(-1.0f, std::min(1.0f, in.spec_snap.time_q[i]));
      int px = cx + (int)llround(ii * (float)(draw_rect.width() / 2 - 2));
      int py = cy - (int)llround(qq * (float)(draw_rect.height() / 2 - 2));
      p.drawEllipse(QPoint(px, py), 2, 2);
    }
  }

  const QByteArray const_title =
      gui_i18n_text(in.language, "monitor.constellation").toUtf8();
  draw_panel_title(p, panel_x, panel_y, panel_h, const_title.constData());
  p.setPen(QColor("#c4d2e4"));
  draw_monitor_x_labels(p, draw_rect, draw_rect.bottom() + 8, "-1", "0", "+1");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.top(), 48, 14), Qt::AlignRight | Qt::AlignVCenter, "+1");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.center().y(), 48, 14), Qt::AlignRight | Qt::AlignVCenter, "0");
  p.drawText(monitor_y_label_rect(draw_rect, draw_rect.bottom(), 48, 14), Qt::AlignRight | Qt::AlignVCenter, "-1");
}

void map_update_waterfall_image(int win_width, int win_height,
                                const SpectrumSnapshot &snap, QImage *image,
                                int *image_width, int *image_height) {
  if (!image || !image_width || !image_height)
    return;

  int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
  get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w,
                       &panel_h, true);
  if (panel_w < 180 || panel_h < 120)
    return;

  QRect draw_rect = monitor_plot_rect(panel_x, panel_y, panel_w, panel_h);
  int draw_w = draw_rect.width();
  int draw_h = draw_rect.height();
  if (draw_w < 8 || draw_h < 8)
    return;

  if (*image_width != draw_w || *image_height != draw_h || image->isNull()) {
    *image = QImage(draw_w, draw_h, QImage::Format_RGB32);
    image->fill(qRgb(8, 20, 35));
    *image_width = draw_w;
    *image_height = draw_h;
  }

  if (!snap.valid || snap.bins < 8)
    return;

  if (draw_h > 1) {
    for (int y = draw_h - 1; y > 0; --y) {
      std::memcpy(image->scanLine(y), image->scanLine(y - 1),
                  (size_t)draw_w * 4u);
    }
  }

  uint32_t *row = reinterpret_cast<uint32_t *>(image->scanLine(0));
  for (int x = 0; x < draw_w; ++x) {
    int bin = (int)((long long)x * snap.bins / draw_w);
    if (bin < 0)
      bin = 0;
    if (bin >= snap.bins)
      bin = snap.bins - 1;
    uint8_t r = 0, g = 0, b = 0;
    rel_db_to_rgb(snap.rel_db[bin], &r, &g, &b);
    r = (uint8_t)std::max(10, (int)r);
    g = (uint8_t)std::max(26, (int)g);
    b = (uint8_t)std::max(54, (int)b);
    row[x] = qRgb(r, g, b);
  }
}