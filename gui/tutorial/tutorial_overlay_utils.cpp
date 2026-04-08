#include "gui/tutorial/tutorial_overlay_utils.h"

#include "gui/core/gui_font_manager.h"
#include "gui/core/gui_i18n.h"
#include "gui/layout/control_layout.h"
#include "gui/layout/quad_panel_layout.h"
#include "gui/control/control_paint.h"
#include "gui/geo/geo_io.h"
#include "gui/tutorial/tutorial_flow_utils.h"

#include <QPainterPath>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#if __has_include(<imgui.h>)
#include <imgui.h>
#define TUTORIAL_HAS_IMGUI 1
#else
#define TUTORIAL_HAS_IMGUI 0
#endif

namespace {

constexpr uint8_t kSignalModeBds = 0;
constexpr uint8_t kSignalModeGps = 1;
constexpr uint8_t kSignalModeMixed = 2;

double clamp01(double t) {
  if (t < 0.0) return 0.0;
  if (t > 1.0) return 1.0;
  return t;
}

double lerp(double a, double b, double t) {
  return a + (b - a) * t;
}

QRectF lerp_rect(const QRectF &a, const QRectF &b, double t) {
  return QRectF(lerp(a.x(), b.x(), t), lerp(a.y(), b.y(), t),
                lerp(a.width(), b.width(), t),
                lerp(a.height(), b.height(), t));
}

QRectF fit_rect_center_scaled(const QRectF &r, double scale) {
  if (r.isEmpty()) return r;
  const QPointF c = r.center();
  const double w = std::max(1.0, r.width() * scale);
  const double h = std::max(1.0, r.height() * scale);
  return QRectF(c.x() - w * 0.5, c.y() - h * 0.5, w, h);
}

QPainterPath torn_bottom_clip_path(const QRectF &rect, double amp_px,
                                  double freq_cycles) {
  QPainterPath path;
  if (rect.isEmpty()) return path;

  const double left = rect.left();
  const double right = rect.right();
  const double top = rect.top();
  const double base = rect.bottom() - std::max(2.0, amp_px * 0.55);
  const double w = std::max(1.0, rect.width());

  path.moveTo(left, top);
  path.lineTo(right, top);
  path.lineTo(right, base);
  for (int i = 60; i >= 0; --i) {
    const double t = (double)i / 60.0;
    const double x = left + w * t;
    const double y = base + amp_px *
        (0.66 * std::sin(t * freq_cycles * 2.0 * M_PI + 0.35) +
         0.34 * std::sin(t * (freq_cycles * 3.0) * 2.0 * M_PI + 1.10));
    path.lineTo(x, y);
  }
  path.closeSubpath();
  return path;
}

QPainterPath torn_top_clip_path(const QRectF &rect, double amp_px,
                               double freq_cycles) {
  QPainterPath path;
  if (rect.isEmpty()) return path;

  const double left = rect.left();
  const double right = rect.right();
  const double bottom = rect.bottom();
  const double base = rect.top() + std::max(2.0, amp_px * 0.55);
  const double w = std::max(1.0, rect.width());

  path.moveTo(left, bottom);
  path.lineTo(right, bottom);
  path.lineTo(right, base);
  for (int i = 60; i >= 0; --i) {
    const double t = (double)i / 60.0;
    const double x = left + w * t;
    const double y = base + amp_px *
        (0.64 * std::sin(t * freq_cycles * 2.0 * M_PI + 0.90) +
         0.36 * std::sin(t * (freq_cycles * 2.7) * 2.0 * M_PI + 0.15));
    path.lineTo(x, y);
  }
  path.closeSubpath();
  return path;
}

QPixmap build_map_torn_composite(const QPixmap &map_snapshot) {
  if (map_snapshot.isNull()) return QPixmap();
  const int w = std::max(16, map_snapshot.width());
  const int h = std::max(16, map_snapshot.height());
  const int out_h = std::max(20, h / 2);
  const int gap = std::max(10, std::min(22, out_h / 12));
  const int piece_h = std::max(6, (out_h - gap) / 2);

  const QRect src_top(0, 0, w, std::max(1, h / 4));
  const QRect src_bottom(0, std::max(0, h - std::max(1, h / 4)), w,
                         std::max(1, h / 4));
  const QRectF dst_top(0.0, 0.0, (double)w, (double)piece_h);
  const QRectF dst_bottom(0.0, (double)(piece_h + gap), (double)w,
                          (double)piece_h);

  QPixmap out(w, out_h);
  out.fill(Qt::transparent);

  QPainter qp(&out);
  qp.setRenderHint(QPainter::Antialiasing, true);

  const double amp = std::max(2.0, piece_h * 0.10);
  const QPainterPath clip_top = torn_bottom_clip_path(dst_top, amp, 4.2);
  const QPainterPath clip_bottom = torn_top_clip_path(dst_bottom, amp, 4.2);

  qp.save();
  qp.setClipPath(clip_top);
  qp.drawPixmap(dst_top.toRect(), map_snapshot, src_top);
  qp.restore();

  qp.save();
  qp.setClipPath(clip_bottom);
  qp.drawPixmap(dst_bottom.toRect(), map_snapshot, src_bottom);
  qp.restore();

  // Visible torn gap and subtle paper-edge shadow/highlight for realism.
  const QRect gap_rect(0, piece_h, w, gap);
  qp.fillRect(gap_rect, QColor(6, 10, 18, 230));

  qp.setPen(QPen(QColor(8, 14, 24, 170), 1));
  qp.drawPath(clip_top);
  qp.drawPath(clip_bottom);

  qp.setPen(QPen(QColor(255, 255, 255, 36), 1));
  qp.drawLine(2, piece_h - 1, w - 2, piece_h - 1);
  qp.drawLine(2, piece_h + gap, w - 2, piece_h + gap);

  qp.end();
  return out;
}

QPointF rect_edge_anchor_toward(const QRectF &r, const QPointF &dst) {
  if (r.isEmpty()) return dst;

  const QPointF c = r.center();
  const double dx = dst.x() - c.x();
  const double dy = dst.y() - c.y();
  if (std::abs(dx) < 1e-6 && std::abs(dy) < 1e-6) {
    return QPointF(r.right(), c.y());
  }

  const double hw = std::max(1e-6, r.width() * 0.5);
  const double hh = std::max(1e-6, r.height() * 0.5);
  const double tx = std::abs(dx) < 1e-6 ? 1e18 : hw / std::abs(dx);
  const double ty = std::abs(dy) < 1e-6 ? 1e18 : hh / std::abs(dy);
  const double t = std::min(tx, ty);
  return QPointF(c.x() + dx * t, c.y() + dy * t);
}

std::vector<TutorialGalaxyCalloutDef>
build_default_callouts(TutorialGalaxySceneId scene_id, GuiLanguage language) {
  auto tr = [&](const char *key) { return gui_i18n_text(language, key); };

  std::vector<TutorialGalaxyCalloutDef> out;
  if (scene_id == TutorialGalaxySceneId::Map) {
    out.push_back({"map_top_btn", tr("tutorial.callout.default.map_top_btn"),
                   -120.0, 240.0, QSize(240, 84)});
    out.push_back({"map_top_info", tr("tutorial.callout.default.map_top_info"),
                   -58.0, 260.0, QSize(240, 84)});
    out.push_back({"map_bottom_left", tr("tutorial.callout.default.map_bottom_left"),
                   132.0, 260.0, QSize(240, 84)});
    out.push_back({"map_bottom_right", tr("tutorial.callout.default.map_bottom_right"),
                   48.0, 260.0, QSize(240, 84)});
  } else if (scene_id == TutorialGalaxySceneId::SubSatellitePoint) {
    out.push_back({"ssp_g", tr("tutorial.callout.default.ssp_g"),
                   -38.0, 230.0, QSize(220, 84)});
    out.push_back({"ssp_c", tr("tutorial.callout.default.ssp_c"),
                   148.0, 230.0, QSize(220, 84)});
  } else if (scene_id == TutorialGalaxySceneId::SignalSetting) {
    out.push_back({"sig_simple", tr("tutorial.callout.default.sig_simple"),
                   -128.0, 248.0, QSize(228, 88)});
    out.push_back({"sig_detail", tr("tutorial.callout.default.sig_detail"),
                   52.0, 248.0, QSize(228, 88)});
  } else if (scene_id == TutorialGalaxySceneId::Waveform) {
    out.push_back({"wave_amp", tr("tutorial.callout.default.wave_amp"),
                   -118.0, 260.0, QSize(240, 88)});
    out.push_back({"wave_bw", tr("tutorial.callout.default.wave_bw"),
                   -18.0, 238.0, QSize(220, 84)});
    out.push_back({"wave_noise", tr("tutorial.callout.default.wave_noise"),
                   72.0, 238.0, QSize(220, 84)});
    out.push_back({"wave_mode", tr("tutorial.callout.default.wave_mode"),
                   162.0, 258.0, QSize(230, 88)});
  }
  return out;
}

} // namespace

std::vector<QPointF>
calculateRadialPositions(const QPointF &center,
                        const std::vector<TutorialGalaxyCalloutDef> &callouts,
                        double radius_scale) {
  std::vector<QPointF> out;
  out.reserve(callouts.size());
  const double rs = std::max(0.1, radius_scale);

  for (const TutorialGalaxyCalloutDef &def : callouts) {
    const double theta = def.angle_deg * 3.14159265358979323846 / 180.0;
    const double r = std::max(20.0, def.radius_px * rs);
    out.push_back(QPointF(center.x() + std::cos(theta) * r,
                          center.y() + std::sin(theta) * r));
  }
  return out;
}

TutorialGalaxySceneDef tutorial_build_default_galaxy_scene(
    TutorialGalaxySceneId scene_id, QWidget *target_widget,
    GuiLanguage language) {
  TutorialGalaxySceneDef scene;
  scene.scene_id = scene_id;
  scene.target_widget = target_widget;
  scene.center_scale = 1.08;
  scene.mask_alpha = 192;
  scene.move_duration_sec = 0.72;
  scene.callouts = build_default_callouts(scene_id, language);
  return scene;
}

void TutorialGalaxyOverlayController::begin(const TutorialGalaxySceneDef &scene,
                                            QWidget *overlay_widget) {
  running_ = false;
  overlay_widget_ = overlay_widget;
  scene_ = scene;
  snapshot_ = QPixmap();
  source_rect_ = QRect();
  start_rect_ = QRectF();
  target_rect_ = QRectF();

  if (!overlay_widget_ || !scene_.target_widget) return;

  QRect local = scene_.target_local_rect;
  if (local.isEmpty()) {
    local = scene_.target_widget->rect();
  }
  if (local.width() <= 1 || local.height() <= 1) return;

  const QPoint top_left = scene_.target_widget->mapTo(overlay_widget_, local.topLeft());
  source_rect_ = QRect(top_left, local.size()).intersected(overlay_widget_->rect());
  if (source_rect_.isEmpty()) return;

  QPixmap grabbed = scene_.target_widget->grab(local);
  if (grabbed.isNull()) return;
  snapshot_ = grabbed;

  start_rect_ = QRectF(source_rect_);
  running_ = true;
  start_tp_ = std::chrono::steady_clock::now();
}

void TutorialGalaxyOverlayController::stop() {
  running_ = false;
  snapshot_ = QPixmap();
  source_rect_ = QRect();
  start_rect_ = QRectF();
  target_rect_ = QRectF();
}

bool TutorialGalaxyOverlayController::visible() const {
  return running_;
}

bool TutorialGalaxyOverlayController::handleMousePress(const QPoint &pos) const {
  if (!running_) return false;
  Q_UNUSED(pos);
  return true;
}

bool TutorialGalaxyOverlayController::handleKeyPress(int key) const {
  if (!running_) return false;
  return key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter;
}

void TutorialGalaxyOverlayController::tick() {
  if (!running_ || !overlay_widget_) return;
  overlay_widget_->update();
}

TutorialGalaxyRenderFrame
TutorialGalaxyOverlayController::buildFrame(const QRect &viewport_rect) const {
  TutorialGalaxyRenderFrame frame;
  frame.source_rect = QRectF(source_rect_);

  if (!running_ || snapshot_.isNull() || source_rect_.isEmpty() || viewport_rect.isEmpty()) {
    return frame;
  }

  const QRectF viewport(viewport_rect);
  const QRectF source(source_rect_);

  const double scale = std::max(1.0, scene_.center_scale);
  const QSizeF scaled_size(source.width() * scale, source.height() * scale);
  const QPointF dst_tl(viewport.center().x() - scaled_size.width() * 0.5,
                       viewport.center().y() - scaled_size.height() * 0.5);
  const QRectF dst_rect(dst_tl, scaled_size);

  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - start_tp_)
                             .count();
  const double duration = std::max(0.05, scene_.move_duration_sec);
  const double t = clamp01(elapsed / duration);
  const double eased = 1.0 - std::pow(1.0 - t, 3.0);

  frame.progress = eased;
  frame.snapshot_rect = lerp_rect(source, dst_rect, eased);

  const std::vector<QPointF> radial_centers =
      calculateRadialPositions(frame.snapshot_rect.center(), scene_.callouts, 1.0);
  frame.nodes.reserve(scene_.callouts.size());

  for (int i = 0; i < (int)scene_.callouts.size() && i < (int)radial_centers.size(); ++i) {
    TutorialGalaxyNodeLayout node;
    node.def = scene_.callouts[i];
    node.center = radial_centers[i];
    const QSize bs = node.def.box_size;
    node.box_rect = QRectF(node.center.x() - bs.width() * 0.5,
                           node.center.y() - bs.height() * 0.5,
                           std::max(40, bs.width()), std::max(30, bs.height()));
    node.edge_anchor = rect_edge_anchor_toward(frame.snapshot_rect, node.center);
    frame.nodes.push_back(node);
  }

  return frame;
}

void TutorialGalaxyOverlayController::paint(QPainter &p,
                                            const QRect &viewport_rect) const {
  if (!running_ || viewport_rect.isEmpty()) return;

  p.save();
  p.fillRect(viewport_rect, QColor(6, 10, 18, std::max(0, std::min(255, scene_.mask_alpha))));

  TutorialGalaxyRenderFrame frame = buildFrame(viewport_rect);
  if (snapshot_.isNull() || frame.snapshot_rect.isEmpty()) {
    p.restore();
    return;
  }

  p.setRenderHint(QPainter::Antialiasing, true);

  const QRectF halo = fit_rect_center_scaled(frame.snapshot_rect, 1.02 + 0.05 * frame.progress);
  p.setPen(QPen(QColor(125, 211, 252, 160), 3));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(halo, 12, 12);

  p.drawPixmap(frame.snapshot_rect.toRect(), snapshot_);

  QFont old_font = p.font();
  QFont text_font = old_font;
  text_font.setPointSize(std::max(10, old_font.pointSize()));
  p.setFont(text_font);

  for (const TutorialGalaxyNodeLayout &node : frame.nodes) {
    p.setPen(QPen(QColor(147, 197, 253, 220), 2));
    p.drawLine(node.edge_anchor, node.center);

    p.setPen(QPen(QColor(186, 230, 253, 200), 1));
    p.setBrush(QColor(11, 24, 42, 232));
    p.drawRoundedRect(node.box_rect, 9, 9);

    p.setPen(QColor(229, 238, 252));
    p.drawText(node.box_rect.adjusted(10, 8, -10, -8),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, node.def.text);
  }

  p.setFont(old_font);
  p.restore();
}

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in) {
  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.detailed);

  const QRect left_map = in.osm_panel_rect.adjusted(6, 6, -6, -6);
  if (in.step == 0) {
    // TOC step: no focused region
    return QRect();
  }
  if (in.step == 1) {
    return QRect(left_map.x(), left_map.y(), left_map.width(),
                 std::max(12, left_map.height() / 2));
  }
  if (in.step == 2) {
    return QRect(left_map.x(), left_map.y() + std::max(12, left_map.height() / 2),
                 left_map.width(), std::max(12, left_map.height() / 2));
  }
  if (in.step == 3) {
    int map_x = in.win_width / 2;
    int map_y = 0;
    int map_w = in.win_width - map_x;
    int map_h = in.win_height / 2;
    return QRect(map_x + 6, map_y + 6, std::max(10, map_w - 12),
                 std::max(10, map_h - 12));
  }
  if (in.step == 4) {
    int x = in.win_width / 2;
    int y = in.win_height / 2;
    int w = in.win_width - x;
    int h = in.win_height - y;
    return QRect(x + 6, y + 6, std::max(10, w - 12), std::max(10, h - 12));
  }
  if (in.step == 5) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  if (in.step == 6) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  if (in.step == 7 || in.step == 8) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  return QRect();
}

void tutorial_draw_overlay(QPainter &p, const TutorialOverlayInput &in,
                           TutorialOverlayState *state) {
  static bool s_capture_in_progress = false;
  static bool s_session_frozen = false;
  static QWidget *s_frozen_host = nullptr;
  static QSize s_frozen_size;
  static QPixmap s_frozen_background;

  if (s_capture_in_progress) return;
  if (!state) return;
  if (!in.overlay_visible) {
    s_session_frozen = false;
    s_frozen_host = nullptr;
    s_frozen_size = QSize();
    s_frozen_background = QPixmap();
    state->prev_btn_rect = QRect();
    state->next_btn_rect = QRect();
    state->close_btn_rect = QRect();
    return;
  }

  auto tr_key = [&](const char *key) {
    return gui_i18n_text(in.language, key);
  };

  const int step = std::max(0, std::min(in.step, in.last_step));
  TutorialOverlayInput focus_in = in;
  focus_in.step = step;
  QRect focus = tutorial_focus_rect_for_step(focus_in);

  if (focus.isEmpty() && step != 0) {
    const int w = std::max(220, in.win_width / 3);
    const int h = std::max(160, in.win_height / 4);
    focus = QRect((in.win_width - w) / 2, (in.win_height - h) / 2, w, h);
  }

  // ── TOC page (Part 1 / Contents) ──────────────────────────────────────────
  if (step == 0) {
    // Capture background immediately on guide open so PREV/NEXT share the same freeze.
    {
      const bool need_freeze_toc = !s_session_frozen ||
                                   s_frozen_host != in.host_widget ||
                                   s_frozen_size != QSize(in.win_width, in.win_height) ||
                                   s_frozen_background.isNull();
      if (need_freeze_toc && in.host_widget && !s_capture_in_progress) {
        s_capture_in_progress = true;
        QWidget *top_widget = in.host_widget->window();
        QScreen *screen = nullptr;
        if (top_widget && top_widget->windowHandle())
          screen = top_widget->windowHandle()->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
          const QPoint tlg = in.host_widget->mapToGlobal(QPoint(0, 0));
          s_frozen_background = screen->grabWindow(0, tlg.x(), tlg.y(),
                                                   in.host_widget->width(),
                                                   in.host_widget->height());
          s_frozen_host = in.host_widget;
          s_frozen_size = QSize(in.win_width, in.win_height);
          s_session_frozen = !s_frozen_background.isNull();
        }
        s_capture_in_progress = false;
      }
    }

    const int fs = std::max(10, std::min(14, in.win_height / 60));
    QFont toc_font = gui_font_ui(in.language, fs);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── Background: frozen capture at very low opacity + dark gradient ──
    if (!s_frozen_background.isNull()) {
      p.setOpacity(0.07);
      p.drawPixmap(0, 0, s_frozen_background);
      p.setOpacity(1.0);
    }
    QLinearGradient bg_grad(0, 0, 0, in.win_height);
    bg_grad.setColorAt(0.0, QColor(4, 8, 20, 242));
    bg_grad.setColorAt(0.5, QColor(5, 12, 30, 232));
    bg_grad.setColorAt(1.0, QColor(4, 8, 20, 246));
    p.fillRect(QRect(0, 0, in.win_width, in.win_height), bg_grad);

    // ── Dot-grid decoration ──
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(56, 189, 248, 18));
    const int gs = 38;
    for (int gx = gs / 2; gx < in.win_width + gs; gx += gs)
      for (int gy = gs / 2; gy < in.win_height + gs; gy += gs)
        p.drawEllipse(QPointF(gx, gy), 1.4, 1.4);

    // ── Horizontal accent rules ──
    const int hdr_h = 52, ftr_h = 52;
    p.setPen(QPen(QColor(56, 189, 248, 36), 1));
    p.drawLine(0, hdr_h - 1, in.win_width, hdr_h - 1);
    p.drawLine(0, in.win_height - ftr_h, in.win_width, in.win_height - ftr_h);

    // ── Header ──
    QFont hdr_font = toc_font;
    hdr_font.setLetterSpacing(QFont::AbsoluteSpacing, 2.8);
    hdr_font.setPointSize(fs + 1);
    p.setFont(hdr_font);
    p.setPen(QColor(125, 211, 252));
    const QString hdr_txt = gui_i18n_text(in.language, "tutorial.overlay.toc_header");
    p.drawText(QRect(0, 0, in.win_width, hdr_h - 1), Qt::AlignHCenter | Qt::AlignVCenter, hdr_txt);


    // ── Section cards ──
    struct TocEntry {
      const char *label_key;
      const char *range_key;
      int r, g, b;
    };
    static const TocEntry kE[5] = {
      {"tutorial.toc.section.map", "tutorial.toc.range.map", 30, 120, 220},
      {"tutorial.toc.section.skyplot", "tutorial.toc.range.skyplot", 30, 180, 90},
      {"tutorial.toc.section.waveforms", "tutorial.toc.range.waveforms", 200, 100, 20},
      {"tutorial.toc.section.simple", "tutorial.toc.range.simple", 130, 50, 200},
      {"tutorial.toc.section.detail", "tutorial.toc.range.detail", 20, 140, 180},
    };
    static const int kTargets[5] = {1, 3, 4, 5, 7};

    // Adaptive card geometry
    const int btn_bar_h = 46;
    const int avail_h = in.win_height - hdr_h - ftr_h - btn_bar_h;
    const int bh = std::min(76, std::max(50, avail_h / 6));
    const int bgap = std::max(8, bh / 7);
    const int total_cards_h = 5 * bh + 4 * bgap;
    const int bw = std::min(580, std::max(320, (int)(in.win_width * 0.58)));
    const int bx = (in.win_width - bw) / 2;
    const int base_y = hdr_h + (avail_h - total_cards_h) / 2;

    // Badge geometry
    const int badge_r = std::max(12, bh / 2 - 9);
    const int badge_diam = badge_r * 2;
    const int pill_w = 116;
    // Non-overlapping regions:
    // badge area: [bx+4, bx+4+badge_diam+10]
    // label area: [bx+4+badge_diam+16, bx+bw-pill_w-14]
    // pill area:  [bx+bw-pill_w-6, bx+bw-6]
    const int label_start_x = bx + 4 + badge_diam + 16;
    const int label_end_x   = bx + bw - pill_w - 14;

    QFont badge_f = toc_font;
    badge_f.setPointSize(std::max(8, fs - 1));

    QFont label_f = toc_font;

    QFont pill_f = toc_font;
    pill_f.setPointSize(std::max(8, fs - 2));

    for (int i = 0; i < 5; ++i) {
      state->toc_btn_targets[i] = kTargets[i];
      const QRect btn(bx, base_y + i * (bh + bgap), bw, bh);
      state->toc_btn_rects[i] = btn;
      const int cy = btn.y() + btn.height() / 2;
      const int R = kE[i].r, G = kE[i].g, B = kE[i].b;

      // Card fill (very dark tinted)
      p.setBrush(QColor(R / 9, G / 9, B / 9 + 5, 225));
      p.setPen(Qt::NoPen);
      p.drawRoundedRect(btn, 7, 7);

      // Outer glow halo
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(QColor(R, G, B, 48), 3));
      p.drawRoundedRect(btn.adjusted(-2, -2, 2, 2), 9, 9);

      // Inner accent border
      p.setPen(QPen(QColor(R, G, B, 135), 1.5));
      p.drawRoundedRect(btn, 7, 7);

      // Left accent bar
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(R, G, B, 210));
      p.drawRoundedRect(QRect(btn.x(), btn.y() + 5, 4, btn.height() - 10), 2, 2);

      // Number badge circle
      const QPointF badge_c(bx + 4 + badge_r + 8, cy);
      p.setBrush(QColor(R, G, B, 40));
      p.setPen(QPen(QColor(R, G, B, 165), 1.5));
      p.drawEllipse(badge_c, (double)badge_r, (double)badge_r);

      // Badge number text
      p.setFont(badge_f);
      const int br = std::min(255, R + 85), bg = std::min(255, G + 85), bb = std::min(255, B + 85);
      p.setPen(QColor(br, bg, bb));
      p.drawText(QRectF(badge_c.x() - badge_r, badge_c.y() - badge_r,
                        badge_diam, badge_diam), Qt::AlignCenter,
                 QString::number(kTargets[i]));

      // Section name (label region, guaranteed non-overlapping)
      p.setFont(label_f);
      p.setPen(QColor(228, 244, 255));
      const QString lbl = gui_i18n_text(in.language, kE[i].label_key);
      p.drawText(QRect(label_start_x, btn.y(), label_end_x - label_start_x, bh),
                 Qt::AlignLeft | Qt::AlignVCenter, lbl);

      // Range pill (right, guaranteed non-overlapping)
      const QRect pill(bx + bw - pill_w - 6, cy - 11, pill_w, 22);
      p.setBrush(QColor(R, G, B, 28));
      p.setPen(QPen(QColor(R, G, B, 100), 1));
      p.drawRoundedRect(pill, 11, 11);
      p.setFont(pill_f);
      const int pr = std::min(255, R + 65), pg = std::min(255, G + 65), pb = std::min(255, B + 65);
      p.setPen(QColor(pr, pg, pb, 210));
      const QString rng_str = gui_i18n_text(in.language, kE[i].range_key);
      p.drawText(pill, Qt::AlignCenter, rng_str);
    }

    // ── Nav buttons (PREV, NEXT, EXIT — no CONTENTS on TOC itself) ──
    const int nbw = 98, nbh = 28, nbgap = 12;
    const int nbar_y = in.win_height - nbh - 14;
    const int ntotal_w = nbw * 3 + nbgap * 2;
    const int nbar_x = std::max(10, (in.win_width - ntotal_w) / 2);
    state->prev_btn_rect     = QRect(nbar_x, nbar_y, nbw, nbh);
    state->next_btn_rect     = QRect(nbar_x + nbw + nbgap, nbar_y, nbw, nbh);
    state->close_btn_rect    = QRect(nbar_x + (nbw + nbgap) * 2, nbar_y, nbw, nbh);
    state->contents_btn_rect = QRect();

    auto draw_toc_nav = [&](const QRect &r, const QColor &fill,
                            const QColor &tc, const char *s) {
      Rect rr{r.x(), r.y(), r.width(), r.height()};
      control_draw_button_filled(p, rr, fill, fill, tc, s);
    };
    p.setFont(toc_font);
    draw_toc_nav(state->prev_btn_rect, QColor("#334155"), QColor("#f8fbff"),
                 gui_i18n_text(in.language, "tutorial.btn.prev").toUtf8().constData());
    draw_toc_nav(state->next_btn_rect, QColor("#0ea5e9"), QColor(8, 12, 18),
                 gui_i18n_text(in.language, "tutorial.btn.next").toUtf8().constData());
    draw_toc_nav(state->close_btn_rect, QColor("#6b7280"), QColor("#f8fbff"),
                 gui_i18n_text(in.language, "tutorial.btn.exit").toUtf8().constData());

    state->text_page_count = 1;
    if (state->text_page >= 1) state->text_page = 0;
    state->callout_hit_boxes.clear();
    state->callout_hit_anchors.clear();
    p.restore();
    return;
  }
  // ─────────────────────────────────────────────────────────────────────────

  auto now_tp = std::chrono::steady_clock::now();
  if (state->anim_step_anchor != step) {
    state->anim_step_anchor = step;
    state->anim_start_tp = now_tp;
    state->spotlight_index = 0;
    state->text_page = 0;
  }

  const double elapsed =
      std::chrono::duration<double>(now_tp - state->anim_start_tp).count();
  const double duration = 0.68;
  const double t = clamp01(elapsed / duration);
  const double ease = 1.0 - std::pow(1.0 - t, 3.0);
  const bool show_radial_callouts = (t >= 0.98);
  const double pulse = 0.5 + 0.5 * std::sin(elapsed * 2.0 * 3.141592653589793 * 0.85);

  QRect focus_grab = focus;
  if (in.host_widget) {
    focus_grab = focus_grab.intersected(in.host_widget->rect());
  }

  const bool need_freeze = !s_session_frozen || s_frozen_host != in.host_widget ||
                           s_frozen_size != QSize(in.win_width, in.win_height) ||
                           s_frozen_background.isNull();
  if (need_freeze && in.host_widget) {
    s_capture_in_progress = true;
    QWidget *top_widget = in.host_widget->window();
    QScreen *screen = nullptr;
    if (top_widget && top_widget->windowHandle()) {
      screen = top_widget->windowHandle()->screen();
    }
    if (!screen) {
      screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
      const QPoint top_left_global = in.host_widget->mapToGlobal(QPoint(0, 0));
      s_frozen_background = screen->grabWindow(0, top_left_global.x(),
                                               top_left_global.y(),
                                               in.host_widget->width(),
                                               in.host_widget->height());
      s_frozen_host = in.host_widget;
      s_frozen_size = QSize(in.win_width, in.win_height);
      s_session_frozen = !s_frozen_background.isNull();
    }
    s_capture_in_progress = false;
  }

  QPixmap snapshot;
  if (step == 1 && !s_frozen_background.isNull() && !in.osm_panel_rect.isEmpty()) {
    const QRect left_map = in.osm_panel_rect.adjusted(6, 6, -6, -6);
    const QPixmap map_full = s_frozen_background.copy(left_map);
    const QPixmap torn_combo = build_map_torn_composite(map_full);
    if (!torn_combo.isNull()) {
      snapshot = torn_combo;
      focus_grab = QRect(left_map.x(), left_map.y(), left_map.width(),
                         std::max(12, left_map.height() / 2));
    }
  } else if ((step == 5 || step == 6 || step == 7 || step == 8) && !in.signal_clean_snapshot.isNull() &&
      !in.signal_clean_rect.isEmpty()) {
    snapshot = in.signal_clean_snapshot;
    focus_grab = in.signal_clean_rect;
  } else if (step == 4 && !in.waveform_clean_snapshot.isNull() &&
      !in.waveform_clean_rect.isEmpty()) {
    snapshot = in.waveform_clean_snapshot;
    focus_grab = in.waveform_clean_rect;
  } else if (step != 2 && !s_frozen_background.isNull() && !focus_grab.isEmpty()) {
    snapshot = s_frozen_background.copy(focus_grab);
  }

  const double center_scale = (step == 4) ? 1.12 : 1.08;
  const QRectF start_rect = QRectF(focus_grab.isEmpty() ? focus : focus_grab);
  const double dst_w = std::max(120.0, start_rect.width() * center_scale);
  const double dst_h = std::max(90.0, start_rect.height() * center_scale);
  const QRectF dst_rect(in.win_width * 0.5 - dst_w * 0.5,
                        in.win_height * 0.5 - dst_h * 0.5, dst_w, dst_h);
  const QRectF active_rect = lerp_rect(start_rect, dst_rect, ease);
  // Ray geometry is anchored to the final centered body to avoid drifting lines.
  const QRectF ray_rect = dst_rect;

  p.save();
  p.fillRect(QRect(0, 0, in.win_width, in.win_height), QColor(5, 10, 18, 198));

  p.setRenderHint(QPainter::Antialiasing, true);
  if (!snapshot.isNull() && !active_rect.isEmpty()) {
    p.drawPixmap(active_rect.toRect(), snapshot);
  }

  if (step == 2) {
    const QRectF demo = active_rect.adjusted(18, 18, -18, -18);
    const QPointF center = demo.center();

    p.setPen(QPen(QColor(125, 211, 252, 140), 1.4));
    p.setBrush(QColor(10, 22, 34, 92));
    p.drawRoundedRect(demo, 14, 14);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(24, 42, 58, 210));
    p.drawEllipse(center, 124.0, 124.0);

    // Big center mouse icon for interaction tutorial.
    const QRectF mouse_rect(center.x() - 84.0, center.y() - 125.0, 168.0, 250.0);
    QPainterPath mouse_body;
    mouse_body.addRoundedRect(mouse_rect, 48.0, 48.0);
    p.setPen(QPen(QColor(229, 238, 252, 230), 2.2));
    p.setBrush(QColor(15, 23, 36, 230));
    p.drawPath(mouse_body);
    p.drawLine(QPointF(mouse_rect.center().x(), mouse_rect.top() + 14.0),
           QPointF(mouse_rect.center().x(), mouse_rect.top() + 76.0));
  }

  if (step != 2) {
    p.setPen(QPen(QColor(125, 211, 252, 150 + (int)std::lround(80.0 * pulse)), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(fit_rect_center_scaled(active_rect, 1.03 + 0.03 * pulse), 10, 10);
  }

  state->callout_hit_boxes.clear();
  state->callout_hit_anchors.clear();
  if (state->has_glow && state->glow_step != step) state->has_glow = false;

  std::vector<TutorialGalaxyCalloutDef> callouts;
  if (show_radial_callouts) {
    if (step == 1) {
      // Map combined page: place callouts by requested screen-side layout.
      // search box: upper-left edge intersection
      callouts.push_back({"search_box", tr_key("tutorial.callout.step1.search_box"), 225.0, 338.0, QSize(220, 92)});
      // NFZ ON: upper area, slightly left
      callouts.push_back({"nfz_btn", tr_key("tutorial.callout.step1.nfz_btn"), 248.0, 314.0, QSize(220, 92)});
      // ENGLISH: upper area, near center
      callouts.push_back({"lang_btn", tr_key("tutorial.callout.step1.lang_btn"), 272.0, 304.0, QSize(220, 92)});
      // SATELLITE: right side center
      callouts.push_back({"dark_mode_btn", tr_key("tutorial.callout.step1.dark_mode_btn"),   0.0, 312.0, QSize(240, 92)});
      // GUIDE bulb: right side, slightly middle-lower
      callouts.push_back({"guide_btn", tr_key("tutorial.callout.step1.guide_btn"),  24.0, 314.0, QSize(220, 92)});
      // LLH: lower-left edge intersection
      callouts.push_back({"osm_llh", tr_key("tutorial.callout.step2.osm_llh"), 132.0, 330.0, QSize(280, 92)});
    } else if (step == 2) {
      // Part 2: only path tutorial content.
      callouts.push_back({"smart_path", tr_key("tutorial.callout.step2.smart_path"), 240.0, 320.0, QSize(400, 140)});
      callouts.push_back({"straight_path", tr_key("tutorial.callout.step2.straight_path"), 120.0, 320.0, QSize(400, 140)});
      callouts.push_back({"mouse_hint", tr_key("tutorial.callout.step2.mouse_hint"), 0.0, 340.0, QSize(440, 168)});
    } else if (step == 3) {
      const bool show_satellite_callouts = in.has_navigation_data && !in.sat_points.empty();
      const bool show_g_by_mode = (in.signal_mode == kSignalModeGps ||
                                   in.signal_mode == kSignalModeMixed);
      const bool show_c_by_mode = (in.signal_mode == kSignalModeBds ||
                                   in.signal_mode == kSignalModeMixed);
      if (show_satellite_callouts && show_g_by_mode) {
        callouts.push_back({"sky_g", tr_key("tutorial.callout.step3.sky_g"), 328.0, 240.0, QSize(220, 92)});
      }
      if (show_satellite_callouts && show_c_by_mode) {
        callouts.push_back({"sky_c", tr_key("tutorial.callout.step3.sky_c"), 22.0, 245.0, QSize(220, 92)});
      }
    } else if (step == 4) {
      callouts.push_back({"wave_1", tr_key("tutorial.callout.step4.wave_1"), -130.0, 245.0, QSize(220, 92)});
      callouts.push_back({"wave_2", tr_key("tutorial.callout.step4.wave_2"), -45.0, 245.0, QSize(220, 92)});
      callouts.push_back({"wave_3", tr_key("tutorial.callout.step4.wave_3"), 45.0, 245.0, QSize(220, 92)});
      callouts.push_back({"wave_4", tr_key("tutorial.callout.step4.wave_4"), 130.0, 245.0, QSize(220, 92)});
    } else if (step == 5) {
      callouts.push_back({"sig_bdt_gpst", tr_key("tutorial.callout.step5.sig_bdt_gpst"), 332.0, 248.0, QSize(260, 104)});
      callouts.push_back({"sig_rnx", tr_key("tutorial.callout.step5.sig_rnx"), 12.0, 248.0, QSize(260, 104)});
      callouts.push_back({"sig_tab_simple", tr_key("tutorial.callout.step5.sig_tab_simple"), 92.0, 240.0, QSize(240, 104)});
      callouts.push_back({"sig_tab_detail", tr_key("tutorial.callout.step5.sig_tab_detail"), 132.0, 240.0, QSize(240, 104)});
    } else if (step == 6) {
      callouts.push_back({"sig_interfere", tr_key("tutorial.callout.step6.sig_interfere"), 350.0, 250.0, QSize(240, 104)});
      callouts.push_back({"sig_system", tr_key("tutorial.callout.step6.sig_system"), 26.0, 262.0, QSize(240, 104)});
      callouts.push_back({"sig_fs", tr_key("tutorial.callout.step6.sig_fs"), 300.0, 270.0, QSize(240, 104)});
      callouts.push_back({"sig_tx", tr_key("tutorial.callout.step6.sig_tx"), 338.0, 292.0, QSize(256, 104)});
      callouts.push_back({"sig_start", tr_key("tutorial.callout.step6.sig_start"), 40.0, 308.0, QSize(240, 104)});
      callouts.push_back({"sig_exit", tr_key("tutorial.callout.step6.sig_exit"), 12.0, 308.0, QSize(240, 104)});
    } else if (step == 7) {
      // Detail Controls (1/2): sats/signal + path vmax/acc/prn/maxch
      callouts.push_back({"detail_sats",  tr_key("tutorial.callout.step7.detail_sats"), 22.0, 240.0, QSize(220, 104)});
      callouts.push_back({"gain_slider",  tr_key("tutorial.callout.step7.gain_slider"), 180.0, 280.0, QSize(220, 104)});
      callouts.push_back({"cn0_slider",   tr_key("tutorial.callout.step7.cn0_slider"), 240.0, 300.0, QSize(220, 104)});
      callouts.push_back({"path_v_slider",tr_key("tutorial.callout.step7.path_v_slider"), 338.0, 250.0, QSize(230, 104)});
      callouts.push_back({"path_a_slider",tr_key("tutorial.callout.step7.path_a_slider"), 338.0, 300.0, QSize(230, 104)});
      callouts.push_back({"prn_slider",   tr_key("tutorial.callout.step7.prn_slider"), 338.0, 340.0, QSize(230, 104)});
      callouts.push_back({"ch_slider",    tr_key("tutorial.callout.step7.ch_slider"), 350.0, 360.0, QSize(220, 104)});
    } else if (step == 8) {
      // Detail Controls (2/2): format/mode + meo/iono/ext clk
      callouts.push_back({"sw_fmt",  tr_key("tutorial.callout.step8.sw_fmt"), 190.0, 300.0, QSize(220, 104)});
      callouts.push_back({"sw_mode", tr_key("tutorial.callout.step8.sw_mode"), 338.0, 320.0, QSize(220, 104)});
      callouts.push_back({"tg_meo",  tr_key("tutorial.callout.step8.tg_meo"), 45.0, 360.0, QSize(220, 104)});
      callouts.push_back({"tg_iono", tr_key("tutorial.callout.step8.tg_iono"), 180.0, 360.0, QSize(220, 104)});
      callouts.push_back({"tg_clk",  tr_key("tutorial.callout.step8.tg_clk"), 315.0, 360.0, QSize(220, 104)});
    }
  }

  const double kGuideCalloutScaleX = 1.10;
  const double kGuideCalloutScaleY = 1.14;
  for (TutorialGalaxyCalloutDef &callout : callouts) {
    callout.box_size.setWidth(
        (int)std::lround(callout.box_size.width() * kGuideCalloutScaleX));
    callout.box_size.setHeight(
        (int)std::lround(callout.box_size.height() * kGuideCalloutScaleY));
  }

  QFont old_font = p.font();
  QFont text_font = gui_font_ui(in.language, std::max(11, std::min(15, in.win_height / 56)));
  p.setFont(text_font);

  const int btn_h = 28;
  const int btn_gap = 12;
  const int contents_w = 112;
  const int nav_w = 94;
  const int bar_y = in.win_height - btn_h - 14;
  const int total_w = contents_w + nav_w * 3 + btn_gap * 3;
  const int bar_x = std::max(10, (in.win_width - total_w) / 2);

  // Button order: CONTENTS | PREV | NEXT | EXIT
  state->contents_btn_rect = QRect(bar_x, bar_y, contents_w, btn_h);
  state->prev_btn_rect     = QRect(state->contents_btn_rect.right() + 1 + btn_gap,
                                   bar_y, nav_w, btn_h);
  state->next_btn_rect     = QRect(state->prev_btn_rect.right() + 1 + btn_gap,
                                   bar_y, nav_w, btn_h);
  state->close_btn_rect    = QRect(state->next_btn_rect.right() + 1 + btn_gap,
                                   bar_y, nav_w, btn_h);

  const auto map_anchor_to_ray_rect = [&](const QRect &src_rect) -> QPointF {
    if (src_rect.isEmpty()) return ray_rect.center();
    const QPointF src_center = src_rect.center();
    const double sw = std::max(1.0, start_rect.width());
    const double sh = std::max(1.0, start_rect.height());
    double u = (src_center.x() - start_rect.x()) / sw;
    double v = (src_center.y() - start_rect.y()) / sh;
    u = std::max(0.0, std::min(1.0, u));
    v = std::max(0.0, std::min(1.0, v));
    return QPointF(ray_rect.x() + u * ray_rect.width(),
                   ray_rect.y() + v * ray_rect.height());
  };

  const auto map_point_to_ray_rect = [&](const QPointF &src_point) -> QPointF {
    const double sw = std::max(1.0, start_rect.width());
    const double sh = std::max(1.0, start_rect.height());
    double u = (src_point.x() - start_rect.x()) / sw;
    double v = (src_point.y() - start_rect.y()) / sh;
    u = std::max(0.0, std::min(1.0, u));
    v = std::max(0.0, std::min(1.0, v));
    return QPointF(ray_rect.x() + u * ray_rect.width(),
                   ray_rect.y() + v * ray_rect.height());
  };

  // Derive top control button geometry in widget coordinates for stable ray anchors.
  QRect nfz_anchor_rect = in.nfz_btn_rect;
  QRect dark_anchor_rect = in.dark_mode_btn_rect;
  QRect guide_anchor_rect = in.tutorial_toggle_rect;
  QRect lang_anchor_rect = in.lang_btn_rect;
  QRect search_anchor_rect = in.search_box_rect;
  QRect zoom_anchor_rect;
  QRect osm_llh_anchor_rect;
  QRect new_user_anchor_rect;
  QRect nfz_restricted_anchor_rect;
  QRect nfz_warning_anchor_rect;
  QRect nfz_auth_warn_anchor_rect;
  QRect nfz_service_white_anchor_rect;
  QRect wave_1_anchor_rect;
  QRect wave_2_anchor_rect;
  QRect wave_3_anchor_rect;
  QRect wave_4_anchor_rect;
  QRect osm_runtime_anchor_rect = in.osm_runtime_rect;
  QRect osm_stop_anchor_rect = in.osm_stop_btn_rect;
  QRect sig_gear_anchor_rect;
  QRect sig_utc_anchor_rect;
  QRect sig_bdt_anchor_rect;
  QRect sig_gpst_anchor_rect;
  QRect sig_tab_simple_anchor_rect;
  QRect sig_tab_detail_anchor_rect;
  QRect sig_interfere_anchor_rect;
  QRect sig_system_anchor_rect;
  QRect sig_fs_anchor_rect;
  QRect sig_tx_anchor_rect;
  QRect sig_start_anchor_rect;
  QRect sig_exit_anchor_rect;
  QRect detail_sats_anchor_rect;
  QRect seed_slider_anchor_rect;
  QRect gain_slider_anchor_rect;
  QRect cn0_slider_anchor_rect;
  QRect sw_sys_detail_anchor_rect;
  QRect path_v_slider_anchor_rect;
  QRect path_a_slider_anchor_rect;
  QRect prn_slider_anchor_rect;
  QRect ch_slider_anchor_rect;
  QRect sw_fmt_anchor_rect;
  QRect sw_mode_anchor_rect;
  QRect tg_meo_anchor_rect;
  QRect tg_iono_anchor_rect;
  QRect tg_clk_anchor_rect;
  bool step6_two_column_layout = false;
  if ((int)in.osm_status_badge_rects.size() >= 1) {
    osm_llh_anchor_rect = in.osm_status_badge_rects[0];
  } else if (!in.osm_panel_rect.isEmpty()) {
    const int ctrl_btn_w = 90;
    const int ctrl_btn_h = 26;
    const int ctrl_col_gap = 8;
    const int ctrl_row_gap = 8;
    const int col_right_x = in.osm_panel_rect.x() + in.osm_panel_rect.width() - ctrl_btn_w - 8;
    const int col_left_x = col_right_x - ctrl_col_gap - ctrl_btn_w;
    const int row0_y = in.osm_panel_rect.y() + 10;
    const int row1_y = row0_y + ctrl_btn_h + ctrl_row_gap;
    const int row2_y = row1_y + ctrl_btn_h + ctrl_row_gap;

    if (nfz_anchor_rect.isEmpty()) {
      nfz_anchor_rect = QRect(col_left_x, row0_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (dark_anchor_rect.isEmpty()) {
      dark_anchor_rect = QRect(col_right_x, row1_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (guide_anchor_rect.isEmpty()) {
      guide_anchor_rect = QRect(col_right_x, row2_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (lang_anchor_rect.isEmpty()) {
      lang_anchor_rect = QRect(col_right_x, row0_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (search_anchor_rect.isEmpty()) {
      search_anchor_rect = QRect(in.osm_panel_rect.x() + 10,
                                 in.osm_panel_rect.y() + 10, 240, 30);
    }
  }

  // NFZ legend row anchor: use the real rect from snapshot if available,
  // else fall back to an estimated offset derived from panel geometry.
  if ((int)in.osm_nfz_legend_row_rects.size() >= 1) {
    nfz_restricted_anchor_rect = in.osm_nfz_legend_row_rects[0];
  } else if (!in.osm_panel_rect.isEmpty()) {
    const int leg_w = 156;
    const int leg_h = 40;
    const int leg_x = in.osm_panel_rect.x() + in.osm_panel_rect.width() - leg_w - 10;
    const int leg_y = in.osm_panel_rect.y() + in.osm_panel_rect.height() - leg_h - 30;
    nfz_restricted_anchor_rect = QRect(leg_x + 10, leg_y + 8, leg_w - 20, 22);
  }

  if (!in.osm_panel_rect.isEmpty() && zoom_anchor_rect.isEmpty()) {
    // Approximate OSM status badges geometry (bottom-left stack).
    const int pad_y = in.running_ui ? 2 : 4;
    const int line_gap = in.running_ui ? 4 : 6;
    QFont badge_font = text_font;
    if (in.running_ui) {
      badge_font.setPointSize(std::max(8, badge_font.pointSize() - 2));
    }
    QFontMetrics fm_badge(badge_font);
    const int line_h = fm_badge.height() + 2 * pad_y;
    const int base_x = in.osm_panel_rect.x() + 10;
    const int base_y = in.osm_panel_rect.y() + in.osm_panel_rect.height() - 10 - line_h;
    const int lines_count = 3;
    const int first_y = base_y - (lines_count - 1) * (line_h + line_gap);
    const int badge_max_w = std::max(120, in.osm_panel_rect.width() - 24);

    zoom_anchor_rect = QRect(base_x, first_y + 0 * (line_h + line_gap), badge_max_w, line_h);
    new_user_anchor_rect = QRect(base_x, first_y + 1 * (line_h + line_gap), badge_max_w, line_h);
    osm_llh_anchor_rect = QRect(base_x, first_y + 2 * (line_h + line_gap), badge_max_w, line_h);
  }

  if (step == 4) {
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y,
                         &panel_w, &panel_h, false);
    if (panel_w > 0 && panel_h > 0) {
      wave_1_anchor_rect = QRect(panel_x, panel_y, panel_w, panel_h);
    }
    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y,
                         &panel_w, &panel_h, true);
    if (panel_w > 0 && panel_h > 0) {
      wave_2_anchor_rect = QRect(panel_x, panel_y, panel_w, panel_h);
    }
    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y,
                         &panel_w, &panel_h, false);
    if (panel_w > 0 && panel_h > 0) {
      wave_3_anchor_rect = QRect(panel_x, panel_y, panel_w, panel_h);
    }
    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y,
                         &panel_w, &panel_h, true);
    if (panel_w > 0 && panel_h > 0) {
      wave_4_anchor_rect = QRect(panel_x, panel_y, panel_w, panel_h);
    }
  }

  if (step == 5 || step == 6) {
    ControlLayout sig_lo;
    compute_control_layout(in.win_width, in.win_height, &sig_lo, false);
    auto title_band = [](const QRect &r) {
      if (r.isEmpty()) return QRect();
      const int h = std::max(10, std::min(22, r.height() / 2));
      return QRect(r.x() + 6, r.y() + 2, std::max(8, r.width() - 12), h);
    };
    auto rnx_band_from_row = [&](const QRect &r) {
      if (r.isEmpty()) return QRect();
      const int right_w = std::max(24, std::min(std::max(120, r.width() - 72),
                                                (int)std::lround((double)r.width() * 0.46)));
      return QRect(r.right() - right_w + 1, r.y() + 2, std::max(8, right_w - 4),
                   std::max(10, std::min(22, r.height() / 2)));
    };
    auto control_text_head_from_row = [](const QRect &r) {
      if (r.isEmpty()) return QRect();
      const int h = std::max(10, std::min(22, r.height() / 2));
      // Left-start anchor near control label text (INTERFERE/SYSTEM/FS/TX).
      const int w = std::max(26, std::min(56, r.width() / 4));
      return QRect(r.x() + 10, r.y() + 2, w, h);
    };
    auto bdt_gpst_head_from_row = [](const QRect &r) {
      if (r.isEmpty()) return QRect();
      const int h = std::max(10, std::min(22, r.height() / 2));
      // Left-head anchor near "BDT|GPST" text prefix.
      const int w = std::max(20, std::min(40, r.width() / 4));
      return QRect(r.x() + 8, r.y() + 2, w, h);
    };

    const QRect header_gear_rect(sig_lo.header_gear.x, sig_lo.header_gear.y,
                                 sig_lo.header_gear.w, sig_lo.header_gear.h);
    const QRect header_utc_rect(sig_lo.header_utc.x, sig_lo.header_utc.y,
                                sig_lo.header_utc.w, sig_lo.header_utc.h);
    const QRect header_bdt_rect(sig_lo.header_bdt.x, sig_lo.header_bdt.y,
                                sig_lo.header_bdt.w, sig_lo.header_bdt.h);
    const QRect header_gpst_rect(sig_lo.header_gpst.x, sig_lo.header_gpst.y,
                                 sig_lo.header_gpst.w, sig_lo.header_gpst.h);

    sig_gear_anchor_rect = title_band(header_gear_rect);
    sig_utc_anchor_rect = title_band(header_utc_rect);
    sig_bdt_anchor_rect =
      bdt_gpst_head_from_row(header_bdt_rect).united(bdt_gpst_head_from_row(header_gpst_rect));
    sig_gpst_anchor_rect = rnx_band_from_row(header_bdt_rect).united(rnx_band_from_row(header_gpst_rect));

    sig_tab_simple_anchor_rect = title_band(QRect(sig_lo.btn_tab_simple.x, sig_lo.btn_tab_simple.y,
                            sig_lo.btn_tab_simple.w, sig_lo.btn_tab_simple.h));
    sig_tab_detail_anchor_rect = title_band(QRect(sig_lo.btn_tab_detail.x, sig_lo.btn_tab_detail.y,
                            sig_lo.btn_tab_detail.w, sig_lo.btn_tab_detail.h));
    sig_interfere_anchor_rect =
      control_text_head_from_row(QRect(sig_lo.sw_jam.x, sig_lo.sw_jam.y,
                       sig_lo.sw_jam.w, sig_lo.sw_jam.h));
    sig_system_anchor_rect =
      control_text_head_from_row(QRect(sig_lo.sw_sys.x, sig_lo.sw_sys.y,
                       sig_lo.sw_sys.w, sig_lo.sw_sys.h));
    sig_fs_anchor_rect =
      control_text_head_from_row(QRect(sig_lo.fs_slider.x, sig_lo.fs_slider.y,
                       sig_lo.fs_slider.w, sig_lo.fs_slider.h));
    sig_tx_anchor_rect =
      control_text_head_from_row(QRect(sig_lo.tx_slider.x, sig_lo.tx_slider.y,
                       sig_lo.tx_slider.w, sig_lo.tx_slider.h));
    sig_start_anchor_rect = title_band(QRect(sig_lo.btn_start.x, sig_lo.btn_start.y,
                                             sig_lo.btn_start.w, sig_lo.btn_start.h));
    sig_exit_anchor_rect = title_band(QRect(sig_lo.btn_exit.x, sig_lo.btn_exit.y,
                                            sig_lo.btn_exit.w, sig_lo.btn_exit.h));
  }

  if (step == 7 || step == 8) {
    ControlLayout detail_lo;
    compute_control_layout(in.win_width, in.win_height, &detail_lo, true);
    auto rect_from = [](const Rect &r) {
      if (r.w <= 0 || r.h <= 0) return QRect();
      return QRect(r.x, r.y, r.w, r.h);
    };
    auto detail_text_head_from_row = [](const Rect &r) {
      if (r.w <= 0 || r.h <= 0) return QRect();
      const int h = std::max(10, std::min(22, r.h / 2));
      const int w = std::max(26, std::min(68, r.w / 4));
      return QRect(r.x + 10, r.y + 2, w, h);
    };
    // These four are requested to anchor at the beginning of the text line.
    detail_sats_anchor_rect = detail_text_head_from_row(detail_lo.detail_sats);
    gain_slider_anchor_rect = detail_text_head_from_row(detail_lo.gain_slider);
    cn0_slider_anchor_rect = detail_text_head_from_row(detail_lo.cn0_slider);
    sw_sys_detail_anchor_rect = detail_text_head_from_row(detail_lo.sw_sys);
    path_v_slider_anchor_rect = detail_text_head_from_row(detail_lo.path_v_slider);
    path_a_slider_anchor_rect = detail_text_head_from_row(detail_lo.path_a_slider);
    prn_slider_anchor_rect = detail_text_head_from_row(detail_lo.prn_slider);
    ch_slider_anchor_rect = detail_text_head_from_row(detail_lo.ch_slider);
    sw_fmt_anchor_rect = rect_from(detail_lo.sw_fmt);
    sw_mode_anchor_rect = rect_from(detail_lo.sw_mode);
    tg_meo_anchor_rect = rect_from(detail_lo.tg_meo);
    tg_iono_anchor_rect = rect_from(detail_lo.tg_iono);
    tg_clk_anchor_rect = rect_from(detail_lo.tg_clk);

    const int fmt_mode_dx = std::abs(detail_lo.sw_mode.x - detail_lo.sw_fmt.x);
    const bool fmt_mode_stacked_left =
        (fmt_mode_dx <= std::max(40, detail_lo.sw_fmt.w / 3)) &&
        (detail_lo.sw_mode.y > detail_lo.sw_fmt.y + detail_lo.sw_fmt.h / 2);
    const int toggles_min_x = std::min(detail_lo.tg_meo.x,
                               std::min(detail_lo.tg_iono.x, detail_lo.tg_clk.x));
    const int fmt_block_right = std::max(detail_lo.sw_fmt.x + detail_lo.sw_fmt.w,
                                         detail_lo.sw_mode.x + detail_lo.sw_mode.w);
    const bool toggles_right_cluster = toggles_min_x >= (fmt_block_right - 20);
    step6_two_column_layout = fmt_mode_stacked_left && toggles_right_cluster;
  }

  // Final fallback: keep anchors in expected top control band even if panel rect is unavailable.
    if (nfz_anchor_rect.isEmpty() || dark_anchor_rect.isEmpty() ||
      guide_anchor_rect.isEmpty() || lang_anchor_rect.isEmpty() ||
      search_anchor_rect.isEmpty()) {
    const int ctrl_btn_w = 90;
    const int ctrl_btn_h = 26;
    const int ctrl_col_gap = 8;
    const int ctrl_row_gap = 8;
    const int fallback_panel_x = (int)std::lround(start_rect.x());
    const int fallback_panel_y = (int)std::lround(start_rect.y());
    const int fallback_panel_w = std::max(220, (int)std::lround(start_rect.width()));
    const int col_right_x = fallback_panel_x + fallback_panel_w - ctrl_btn_w - 8;
    const int col_left_x = col_right_x - ctrl_col_gap - ctrl_btn_w;
    const int row0_y = fallback_panel_y + 10;
    const int row1_y = row0_y + ctrl_btn_h + ctrl_row_gap;
    const int row2_y = row1_y + ctrl_btn_h + ctrl_row_gap;

    if (nfz_anchor_rect.isEmpty()) {
      nfz_anchor_rect = QRect(col_left_x, row0_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (dark_anchor_rect.isEmpty()) {
      dark_anchor_rect = QRect(col_right_x, row1_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (guide_anchor_rect.isEmpty()) {
      guide_anchor_rect = QRect(col_right_x, row2_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (lang_anchor_rect.isEmpty()) {
      lang_anchor_rect = QRect(col_right_x, row0_y, ctrl_btn_w, ctrl_btn_h);
    }
    if (search_anchor_rect.isEmpty()) {
      search_anchor_rect = QRect(fallback_panel_x + 10,
                                 fallback_panel_y + 10, 240, 30);
    }
  }

  const std::vector<QPointF> radial =
      calculateRadialPositions(ray_rect.center(), callouts, 1.0);
  std::vector<QRectF> placed_boxes;
  placed_boxes.reserve(callouts.size());

  const auto step0_anchor_from_id = [&](const QString &id) -> QPointF {
    const auto status_text_head = [](const QRect &r) -> QRect {
      const int h = std::max(10, std::min(22, r.height() / 2));
      const int w = std::max(26, std::min(68, r.width() / 4));
      return QRect(r.x() + 10, r.y() + 2, w, h);
    };
    if (id == "search_box" && !search_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(search_anchor_rect);
    }
    if (id == "nfz_btn" && !nfz_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(nfz_anchor_rect);
    }
    if (id == "dark_mode_btn" && !dark_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(dark_anchor_rect);
    }
    if (id == "guide_btn" && !guide_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(guide_anchor_rect);
    }
    if (id == "lang_btn" && !lang_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(lang_anchor_rect);
    }
    if (id == "osm_llh" && !osm_llh_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(status_text_head(osm_llh_anchor_rect));
    }
    if (id == "smart_path" || id == "straight_path" || id == "mouse_hint") {
      return ray_rect.center();
    }
    return ray_rect.center();
  };

  const auto step1_anchor_from_id = [&](const QString &id) -> QPointF {
    const auto status_text_head = [](const QRect &r) -> QRect {
      const int h = std::max(10, std::min(22, r.height() / 2));
      const int w = std::max(26, std::min(68, r.width() / 4));
      return QRect(r.x() + 10, r.y() + 2, w, h);
    };
    if (id == "osm_llh" && !osm_llh_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(status_text_head(osm_llh_anchor_rect));
    }
    if (id == "nfz_restricted" && !nfz_restricted_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(nfz_restricted_anchor_rect);
    }
    if (id == "osm_runtime") {
      if (!osm_runtime_anchor_rect.isEmpty()) {
        return map_anchor_to_ray_rect(osm_runtime_anchor_rect);
      }
      if (!in.osm_panel_rect.isEmpty()) {
        const QRect approx(in.osm_panel_rect.x() + 26,
                           in.osm_panel_rect.y() + in.osm_panel_rect.height() - 162,
                           std::max(140, in.osm_panel_rect.width() / 3), 34);
        return map_anchor_to_ray_rect(approx);
      }
    }
    if (id == "osm_stop_btn") {
      if (!osm_stop_anchor_rect.isEmpty()) {
        return map_anchor_to_ray_rect(osm_stop_anchor_rect);
      }
      if (!in.osm_panel_rect.isEmpty()) {
        const QRect approx(in.osm_panel_rect.x() + in.osm_panel_rect.width() / 2 - 70,
                           in.osm_panel_rect.y() + in.osm_panel_rect.height() - 116,
                           140, 40);
        return map_anchor_to_ray_rect(approx);
      }
    }
    if (id == "straight_path" || id == "smart_path" || id == "mouse_hint") {
      const QPointF center = ray_rect.center();
      const QPointF left_btn(center.x() - 36.0, center.y() - 64.0);
      const QPointF right_btn(center.x() + 36.0, center.y() - 64.0);
      if (id == "straight_path") {
        return left_btn;
      }
      if (id == "smart_path") {
        return left_btn;
      }
      return right_btn;
    }
    return ray_rect.center();
  };

  const auto step3_anchor_from_id = [&](const QString &id) -> QPointF {
    if (id == "wave_1" && !wave_1_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(wave_1_anchor_rect);
    }
    if (id == "wave_2" && !wave_2_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(wave_2_anchor_rect);
    }
    if (id == "wave_3" && !wave_3_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(wave_3_anchor_rect);
    }
    if (id == "wave_4" && !wave_4_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(wave_4_anchor_rect);
    }
    return ray_rect.center();
  };

  const auto step4_anchor_from_id = [&](const QString &id) -> QPointF {
    if (id == "sig_gear" && !sig_gear_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_gear_anchor_rect) + QPointF(-4.0, -8.0);
    }
    if (id == "sig_utc" && !sig_utc_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_utc_anchor_rect) + QPointF(6.0, -4.0);
    }
    if (id == "sig_bdt_gpst") {
      if (!sig_bdt_anchor_rect.isEmpty()) return map_anchor_to_ray_rect(sig_bdt_anchor_rect) + QPointF(0.0, 2.0);
      if (!sig_gpst_anchor_rect.isEmpty()) return map_anchor_to_ray_rect(sig_gpst_anchor_rect) + QPointF(4.0, 6.0);
    }
    if (id == "sig_rnx") {
      if (!sig_gpst_anchor_rect.isEmpty()) return map_anchor_to_ray_rect(sig_gpst_anchor_rect) + QPointF(-6.0, 8.0);
    }
    if (id == "sig_tab_simple" && !sig_tab_simple_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_tab_simple_anchor_rect) + QPointF(-8.0, -4.0);
    }
    if (id == "sig_tab_detail" && !sig_tab_detail_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_tab_detail_anchor_rect) + QPointF(8.0, -4.0);
    }
    if (id == "sig_interfere" && !sig_interfere_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_interfere_anchor_rect) + QPointF(0.0, 2.0);
    }
    if (id == "sig_system" && !sig_system_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_system_anchor_rect) + QPointF(0.0, 2.0);
    }
    if (id == "sig_fs" && !sig_fs_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_fs_anchor_rect) + QPointF(0.0, 2.0);
    }
    if (id == "sig_tx" && !sig_tx_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_tx_anchor_rect) + QPointF(0.0, 2.0);
    }
    if (id == "sig_start" && !sig_start_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_start_anchor_rect) + QPointF(-10.0, 8.0);
    }
    if (id == "sig_exit" && !sig_exit_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sig_exit_anchor_rect) + QPointF(-10.0, 12.0);
    }
    return ray_rect.center();
  };

  int slot_left = 0;
  int slot_right = 0;
  int slot_top = 0;
  int slot_bottom = 0;
  const auto slot_offset = [](int idx) {
    static const int pattern[] = {0, -52, 52, -104, 104, -156, 156};
    const int n = (int)(sizeof(pattern) / sizeof(pattern[0]));
    return pattern[idx % n];
  };

  const auto step0_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double cx = ray_rect.center().x();
    const double cy = ray_rect.center().y();
    const double dx = anchor.x() - cx;
    const double dy = anchor.y() - cy;

    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 170.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 170.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 86.0);
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 86.0);
    const double lower_bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                         ray_rect.bottom() + 132.0);

    // Explicit layout request:
    // - nfz/lang: top
    // - dark/guide: right
    // - search_box: left
    if (id == "search_box") {
      const double y = std::max(10.0 + half_h,
                                std::min((double)in.win_height - 58.0 - half_h,
                                         anchor.y() + slot_offset(slot_left++)));
      return QPointF(left_x, y);
    }
    if (id == "nfz_btn" || id == "lang_btn") {
      const double x = std::max(10.0 + half_w,
                                std::min((double)in.win_width - 10.0 - half_w,
                                         anchor.x() + slot_offset(slot_top++)));
      return QPointF(x, top_y);
    }
    if (id == "dark_mode_btn") {
      const QPointF dark_anchor = map_anchor_to_ray_rect(dark_anchor_rect);
      const double y = std::max(10.0 + half_h,
                                std::min((double)in.win_height - 58.0 - half_h,
                                         dark_anchor.y() - 34.0));
      return QPointF(right_x, y);
    }
    if (id == "guide_btn") {
      const QPointF dark_anchor = map_anchor_to_ray_rect(dark_anchor_rect);
      const QPointF guide_anchor = map_anchor_to_ray_rect(guide_anchor_rect);
      const double dark_y = std::max(10.0 + half_h,
                                     std::min((double)in.win_height - 58.0 - half_h,
                                              dark_anchor.y() - 34.0));
      const double min_gap = std::max(72.0, half_h * 2.0 + 16.0);
      const double preferred = guide_anchor.y() + 34.0;
      const double y = std::max(dark_y + min_gap,
                                std::min((double)in.win_height - 58.0 - half_h,
                                         preferred));
      return QPointF(right_x, y);
    }
    if (id == "osm_llh") {
      return QPointF(left_x, lower_bottom_y);
    }

    const bool horizontal = std::abs(dx) >= std::abs(dy) * 1.12;
    if (horizontal) {
      if (dx >= 0.0) {
        const double y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           anchor.y() + slot_offset(slot_right++)));
        return QPointF(right_x, y);
      }
      const double y = std::max(10.0 + half_h,
                                std::min((double)in.win_height - 58.0 - half_h,
                                         anchor.y() + slot_offset(slot_left++)));
      return QPointF(left_x, y);
    }

    if (dy < 0.0) {
      const double x = std::max(10.0 + half_w,
                                std::min((double)in.win_width - 10.0 - half_w,
                                         anchor.x() + slot_offset(slot_top++)));
      return QPointF(x, top_y);
    }
    const double x = std::max(10.0 + half_w,
                              std::min((double)in.win_width - 10.0 - half_w,
                                       anchor.x() + slot_offset(slot_bottom++)));
    return QPointF(x, bottom_y);
  };

  const auto step1_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double center_y = ray_rect.center().y();
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 170.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 170.0);
    const double far_left_x = std::max(10.0 + half_w,
                       ray_rect.left() - half_w - 46.0);
    const double far_right_x = std::min((double)in.win_width - 10.0 - half_w,
                      ray_rect.right() + half_w + 46.0);
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 96.0);
    const double lower_bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                           ray_rect.bottom() + 132.0);

    if (id == "smart_path") {
      const double x = std::min(left_x, far_left_x);
      const double y = std::max(10.0 + half_h, center_y - std::max(94.0, half_h + 26.0));
      return QPointF(x, y);
    }
    if (id == "straight_path") {
      const double x = std::min(left_x, far_left_x);
      const double y = std::min((double)in.win_height - 58.0 - half_h,
                                center_y + std::max(20.0, half_h * 0.18));
      return QPointF(x, y);
    }
    if (id == "mouse_hint") {
      const double x = std::max(right_x, far_right_x);
      const double y = center_y;
      return QPointF(x, y);
    }

    if (id == "zoom") {
      return QPointF(left_x, ray_rect.top() - 88.0);
    }
    if (id == "osm_llh") {
      return QPointF(left_x, lower_bottom_y);
    }
    if (id == "new_user") {
      const double user_y = std::max(10.0 + half_h,
                                     std::min((double)in.win_height - 58.0 - half_h,
                                              anchor.y() - 26.0));
      const double user_x = std::max(left_x,
                                     std::min(left_x + 36.0, anchor.x() - 18.0));
      return QPointF(user_x, user_y);
    }
    // NFZ legend: one restricted item on the right side of the frame
    if (id == "nfz_restricted") {
      const double y = std::max(10.0 + half_h,
                                ray_rect.top() + ray_rect.height() * 0.32);
      return QPointF(right_x, y);
    }
    // STOP / RUN TIME: centred below the frame, split left/right
    if (id == "osm_runtime") {
      const double box_x = std::max(10.0 + half_w,
                                    ray_rect.center().x() - half_w - 30.0);
      return QPointF(box_x, lower_bottom_y);
    }
    if (id == "osm_stop_btn") {
      const double box_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.center().x() + half_w + 30.0);
      return QPointF(box_x, lower_bottom_y);
    }
    return QPointF(right_x, bottom_y);
  };

  // Use the same map_rect as satellite rendering (hardcoded to match main_gui_widget_methods.inl)
  const QRect map_rect(in.win_width / 2, 0, in.win_width - in.win_width / 2,
                       in.win_height / 2);
  
  const auto sat_to_screen = [&](const SatPoint &sat) -> QPointF {
    const int x = map_rect.x() + lon_to_x(sat.lon_deg, map_rect.width());
    const int y = map_rect.y() + lat_to_y(sat.lat_deg, map_rect.height());
    return QPointF(x, y);
  };

  const auto sat_label_enabled_for_overlay = [&](const SatPoint &sat) -> bool {
    if (in.sat_mode == 0) {
      for (int i = 0; i < in.single_candidate_count &&
                      i < (int)in.single_candidates.size(); ++i) {
        if (in.single_candidates[i] == sat.prn) {
          return sat.prn >= 1 && sat.prn < MAX_SAT;
        }
      }
      return false;
    }
    if (in.sat_mode == 1) {
      return sat.is_gps ? (sat.prn >= 1 && sat.prn <= 32)
                        : (sat.prn >= 1 && sat.prn <= 37);
    }
    return true;
  };

  QPointF actual_g_anchor;
  QPointF actual_c_anchor;
  bool has_g_anchor = false;
  bool has_c_anchor = false;

  const bool show_g_by_mode = in.has_navigation_data &&
                              (in.signal_mode == kSignalModeGps ||
                               in.signal_mode == kSignalModeMixed);
  const bool show_c_by_mode = in.has_navigation_data &&
                              (in.signal_mode == kSignalModeBds ||
                               in.signal_mode == kSignalModeMixed);

  for (const SatPoint &sat : in.sat_points) {
    if (!sat_label_enabled_for_overlay(sat)) {
      continue;
    }
    const QPointF sat_gui_anchor = sat_to_screen(sat);
    if (!map_rect.contains(QPoint((int)std::lround(sat_gui_anchor.x()),
                                  (int)std::lround(sat_gui_anchor.y())))) {
      continue;
    }
    const QPointF sat_guide_anchor = map_point_to_ray_rect(sat_gui_anchor);
    if (sat.is_gps) {
      if (!show_g_by_mode) {
        continue;
      }
      if (!has_g_anchor) {
        actual_g_anchor = sat_guide_anchor;
        has_g_anchor = true;
      }
    } else {
      if (!show_c_by_mode) {
        continue;
      }
      if (!has_c_anchor) {
        actual_c_anchor = sat_guide_anchor;
        has_c_anchor = true;
      }
    }
    if (has_g_anchor && has_c_anchor) break;
  }

  const auto step2_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 180.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 180.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 88.0);
    const double mid_y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           ray_rect.center().y() - 12.0));
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 92.0);

    if (id == "bottom_map") {
      return QPointF(left_x, mid_y);
    }
    if (id == "sky_g") {
      if (!anchor.isNull()) {
        const double x = std::max(10.0 + half_w,
                                  std::min((double)in.win_width - 10.0 - half_w,
                                           anchor.x() < ray_rect.center().x()
                                               ? anchor.x() + 84.0
                                               : anchor.x() - 84.0));
        const double y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           anchor.y() - 40.0));
        return QPointF(x, y);
      }
      return QPointF(right_x, top_y);
    }
    if (id == "sky_c") {
      if (!anchor.isNull()) {
        const double x = std::max(10.0 + half_w,
                                  std::min((double)in.win_width - 10.0 - half_w,
                                           anchor.x() < ray_rect.center().x()
                                               ? anchor.x() + 84.0
                                               : anchor.x() - 84.0));
        const double y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           anchor.y() + 36.0));
        return QPointF(x, y);
      }
      return QPointF(right_x, bottom_y);
    }
    return QPointF(right_x, bottom_y);
  };

  const auto step3_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 180.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 180.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 88.0);
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 96.0);

    if (id == "wave_1") {
      return QPointF(left_x, std::min(top_y, anchor.y() - 34.0));
    }
    if (id == "wave_2") {
      return QPointF(left_x, std::max(top_y + 60.0, std::min(bottom_y, anchor.y() + 34.0)));
    }
    if (id == "wave_3") {
      return QPointF(right_x, std::min(top_y, anchor.y() - 34.0));
    }
    if (id == "wave_4") {
      return QPointF(right_x, std::max(top_y + 60.0, std::min(bottom_y, anchor.y() + 34.0)));
    }
    return QPointF(right_x, bottom_y);
  };

  const auto step4_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 190.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 190.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 96.0);
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 100.0);

    const double x_near = std::max(10.0 + half_w,
                                   std::min((double)in.win_width - 10.0 - half_w,
                                            anchor.x()));
    const double y_near = std::max(10.0 + half_h,
                                   std::min((double)in.win_height - 58.0 - half_h,
                                            anchor.y()));

    const double top_y_2 = std::min((double)in.win_height - 58.0 - half_h, top_y + 74.0);
    const double right_inner_x = std::max(10.0 + half_w,
                        std::min((double)in.win_width - 10.0 - half_w,
                             right_x - 14.0));
    const double lower_left_y = std::min((double)in.win_height - 58.0 - half_h,
                       ray_rect.bottom() - 26.0);
    const double lower_right_y = std::min((double)in.win_height - 58.0 - half_h,
                        ray_rect.bottom() - 26.0);

    // Step 5 part-1 fixed slots in requested order.
    if (id == "sig_gear") return QPointF(left_x, top_y);
    if (id == "sig_utc") return QPointF(ray_rect.center().x(), top_y);
    // Pin BDT/GPST to panel top-left with fixed 16px edge spacing and a slight downward offset.
    if (id == "sig_bdt_gpst") {
      const double pinned_x = std::max(10.0 + half_w,
                                       std::min((double)in.win_width - 10.0 - half_w,
                                                ray_rect.left() - 28.0 - half_w));
      const double pinned_y = std::max(10.0 + half_h,
                                       std::min((double)in.win_height - 58.0 - half_h,
                                                ray_rect.top() + 16.0 + half_h + 28.0));
      return QPointF(pinned_x, pinned_y);
    }
    if (id == "sig_rnx") return QPointF(right_inner_x, top_y_2);
    if (id == "sig_tab_simple") return QPointF(left_x, lower_left_y);
    if (id == "sig_tab_detail") return QPointF(right_x, lower_right_y);

    // Fallback for future labels.
    const double d_left = std::abs(anchor.x() - ray_rect.left());
    const double d_right = std::abs(anchor.x() - ray_rect.right());
    const double d_top = std::abs(anchor.y() - ray_rect.top());
    const double d_bottom = std::abs(anchor.y() - ray_rect.bottom());
    if (d_top <= d_left && d_top <= d_right && d_top <= d_bottom) {
      return QPointF(x_near, top_y);
    }
    if (d_left <= d_right && d_left <= d_bottom) {
      return QPointF(left_x, y_near);
    }
    if (d_right <= d_bottom) {
      return QPointF(right_x, y_near);
    }
    return QPointF(x_near, bottom_y);
  };

  const auto step5_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 190.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 190.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 96.0);
    const double mid_y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           ray_rect.center().y() - 6.0));
    const double low_y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                         ray_rect.bottom() - 34.0));
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 94.0);
    const double bottom_center_x = std::max(10.0 + half_w,
                                            std::min((double)in.win_width - 10.0 - half_w,
                                                     ray_rect.center().x()));

    Q_UNUSED(anchor);

    // Step 5 part-2 fixed slots in requested order.
    if (id == "sig_interfere") return QPointF(left_x, top_y);
    if (id == "sig_system") return QPointF(right_x, top_y);
    if (id == "sig_fs") return QPointF(left_x, mid_y);
    if (id == "sig_tx") return QPointF(left_x, low_y);
    if (id == "sig_start") return QPointF(bottom_center_x, bottom_y);
    if (id == "sig_exit") return QPointF(right_x, bottom_y);

    return QPointF(right_x, bottom_y);
  };

  const auto step6_anchor_from_id = [&](const QString &id) -> QPointF {
    if (id == "detail_sats" && !detail_sats_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(detail_sats_anchor_rect);
    }
    if (id == "gain_slider" && !gain_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(gain_slider_anchor_rect);
    }
    if (id == "cn0_slider" && !cn0_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(cn0_slider_anchor_rect);
    }
    if (id == "path_v_slider" && !path_v_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(path_v_slider_anchor_rect);
    }
    if (id == "path_a_slider" && !path_a_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(path_a_slider_anchor_rect);
    }
    if (id == "prn_slider" && !prn_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(prn_slider_anchor_rect);
    }
    if (id == "ch_slider" && !ch_slider_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(ch_slider_anchor_rect);
    }
    if (id == "sw_fmt" && !sw_fmt_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sw_fmt_anchor_rect);
    }
    if (id == "sw_mode" && !sw_mode_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(sw_mode_anchor_rect);
    }
    if (id == "tg_meo" && !tg_meo_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(tg_meo_anchor_rect);
    }
    if (id == "tg_iono" && !tg_iono_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(tg_iono_anchor_rect);
    }
    if (id == "tg_clk" && !tg_clk_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(tg_clk_anchor_rect);
    }
    return ray_rect.center();
  };

  const auto step6_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    const double half_w = std::max(50.0, box_size.width() * 0.5);
    const double half_h = std::max(22.0, box_size.height() * 0.5);
    const double left_x = std::max(10.0 + half_w, ray_rect.left() - 190.0);
    const double right_x = std::min((double)in.win_width - 10.0 - half_w,
                                    ray_rect.right() + 190.0);
    const double top_y = std::max(10.0 + half_h, ray_rect.top() - 96.0);
    const double mid_y = std::max(10.0 + half_h,
                                  std::min((double)in.win_height - 58.0 - half_h,
                                           ray_rect.center().y() - 2.0));
    const double bottom_y = std::min((double)in.win_height - 58.0 - half_h,
                                     ray_rect.bottom() + 94.0);
    const double bottom_center_x = std::max(10.0 + half_w,
                                            std::min((double)in.win_width - 10.0 - half_w,
                                                     ray_rect.center().x()));
    const double bottom_left_x = std::max(10.0 + half_w, ray_rect.left() - 90.0);
    const double bottom_right_x = std::min((double)in.win_width - 10.0 - half_w,
                                           ray_rect.right() + 90.0);
    const double aspect = (double)in.win_width / std::max(1, in.win_height);
    const double upper_y = top_y + ((aspect < 1.7) ? 30.0 : 18.0);

    if (id == "detail_sats") return QPointF(bottom_center_x, top_y);
    if (id == "gain_slider") return QPointF(left_x, upper_y);
    if (id == "cn0_slider") return QPointF(right_x, top_y);
    if (id == "path_v_slider") return QPointF(left_x, mid_y);
    if (id == "path_a_slider") return QPointF(right_x, mid_y);
    if (id == "prn_slider") {
      const double prn_x = std::max(10.0 + half_w,
                                    std::min((double)in.win_width - 10.0 - half_w,
                                             anchor.x()));
      return QPointF(prn_x, bottom_y);
    }
    if (id == "ch_slider") {
      const double ch_x = std::max(10.0 + half_w,
                                   std::min((double)in.win_width - 10.0 - half_w,
                                            anchor.x()));
      return QPointF(ch_x, bottom_y);
    }

    if (step == 8) {
      const double top_fmt_y = std::max(10.0 + half_h,
                                        std::min((double)in.win_height - 58.0 - half_h,
                                                 top_y + 4.0));
      const double mid_mode_y = std::max(10.0 + half_h,
                                         std::min((double)in.win_height - 58.0 - half_h,
                                                  mid_y + 8.0));
      if (id == "sw_fmt") return QPointF(left_x, top_fmt_y);
      if (id == "sw_mode") return QPointF(left_x, mid_mode_y);
      if (id == "tg_meo") return QPointF(bottom_left_x, bottom_y);
      if (id == "tg_iono") return QPointF(bottom_center_x, bottom_y);
      if (id == "tg_clk") return QPointF(bottom_right_x, bottom_y);
    }

    if (!step6_two_column_layout) {
      if (id == "sw_fmt") return QPointF(left_x, mid_y);
      if (id == "sw_mode") return QPointF(right_x, mid_y);
      if (id == "tg_meo") return QPointF(bottom_left_x, bottom_y);
      if (id == "tg_iono") return QPointF(bottom_center_x, bottom_y);
      if (id == "tg_clk") return QPointF(bottom_right_x, bottom_y);
    } else {
      const double left_lower_y = std::min((double)in.win_height - 58.0 - half_h,
                                           mid_y + 56.0);
      const double right_bridge_x = std::max(bottom_center_x + 24.0,
                                     std::min(bottom_right_x - 24.0,
                                              (bottom_center_x + bottom_right_x) * 0.5));
      if (id == "sw_fmt") return QPointF(left_x, mid_y);
      if (id == "sw_mode") return QPointF(left_x, left_lower_y);
      if (id == "tg_meo") return QPointF(bottom_center_x, bottom_y);
      if (id == "tg_iono") return QPointF(right_bridge_x, bottom_y);
      if (id == "tg_clk") return QPointF(right_x, mid_y + 18.0);
    }

    return QPointF(right_x, bottom_y);
  };

  QRectF sky_g_box;
  QRectF sky_c_box;
  bool has_sky_g_box = false;
  bool has_sky_c_box = false;

  for (int i = 0; i < (int)callouts.size() && i < (int)radial.size(); ++i) {
    const TutorialGalaxyCalloutDef &def = callouts[i];
    QPointF c = radial[i];
    QPointF anchor_override = ray_rect.center();
    bool use_anchor_override = false;
    if (step == 1) {
      anchor_override = step0_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step0_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 2) {
      anchor_override = step1_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step1_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 3) {
      if (def.id == "sky_g" && has_g_anchor) {
        anchor_override = actual_g_anchor;
        use_anchor_override = true;
      } else if (def.id == "sky_c" && has_c_anchor) {
        anchor_override = actual_c_anchor;
        use_anchor_override = true;
      } else {
        anchor_override = ray_rect.center();
        use_anchor_override = true;
      }
      c = step2_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 4) {
      anchor_override = step3_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step3_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 5) {
      anchor_override = step4_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step4_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 6) {
      anchor_override = step4_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step5_target_from_anchor(def.id, anchor_override, def.box_size);
    } else if (step == 7 || step == 8) {
      anchor_override = step6_anchor_from_id(def.id);
      use_anchor_override = true;
      c = step6_target_from_anchor(def.id, anchor_override, def.box_size);
    }
    QRectF box(c.x() - def.box_size.width() * 0.5,
               c.y() - def.box_size.height() * 0.5,
               std::max(80, def.box_size.width()),
               std::max(40, def.box_size.height()));
    if (box.left() < 10) box.moveLeft(10);
    if (box.top() < 10) box.moveTop(10);
    if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
    if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);

    if (step == 1) {
      // Push labels outside the main body (sun window).
      const QRectF avoid = ray_rect.adjusted(-8.0, -8.0, 8.0, 8.0);
      QPointF dir = box.center() - ray_rect.center();
      const double len = std::hypot(dir.x(), dir.y());
      if (len > 1e-6) {
        dir = QPointF(dir.x() / len, dir.y() / len);
      } else {
        const double theta = def.angle_deg * 3.14159265358979323846 / 180.0;
        dir = QPointF(std::cos(theta), std::sin(theta));
      }
      int tries = 0;
      while (box.intersects(avoid) && tries < 48) {
        box.translate(dir.x() * 10.0, dir.y() * 10.0);
        ++tries;
      }
      if (box.left() < 10) box.moveLeft(10);
      if (box.top() < 10) box.moveTop(10);
      if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
      if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
    }

    if (step == 3 && (def.id == "sky_g" || def.id == "sky_c")) {
      // Keep G/C explanation cards outside the guide carrier body.
      const QRectF avoid = ray_rect.adjusted(-10.0, -10.0, 10.0, 10.0);
      QPointF dir = box.center() - ray_rect.center();
      const double len = std::hypot(dir.x(), dir.y());
      if (len > 1e-6) {
        dir = QPointF(dir.x() / len, dir.y() / len);
      } else {
        dir = (def.id == "sky_g") ? QPointF(1.0, -0.2) : QPointF(1.0, 0.2);
      }

      int tries = 0;
      while (box.intersects(avoid) && tries < 56) {
        box.translate(dir.x() * 10.0, dir.y() * 10.0);
        ++tries;
      }

      if (box.left() < 10) box.moveLeft(10);
      if (box.top() < 10) box.moveTop(10);
      if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
      if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
    }

    if (step == 2) {
      if (def.id == "osm_llh") {
        const QRectF avoid = QRectF(
            std::max(10.0, ray_rect.left() - 10.0),
            std::min((double)in.win_height - 58.0, ray_rect.bottom() + 72.0),
            std::max(120.0, (double)in.win_width * 0.32),
            120.0);
        int tries = 0;
        while (box.intersects(avoid) && tries < 32) {
          box.translate(-6.0, 8.0);
          if (box.left() < 10) box.moveLeft(10);
          if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
          ++tries;
        }
      } else if (def.id == "new_user") {
        const QRectF avoid = QRectF(ray_rect.center().x() - 40.0,
                                    ray_rect.bottom() + 68.0,
                                    std::max(140.0, (double)in.win_width * 0.38),
                                    120.0);
        int tries = 0;
        while (box.intersects(avoid) && tries < 32) {
          box.translate(8.0, 8.0);
          if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
          if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
          ++tries;
        }
      } else if (def.id == "smart_path" || def.id == "straight_path" || def.id == "mouse_hint") {
        const QRectF avoid = ray_rect.adjusted(-16.0, -16.0, 16.0, 16.0);
        int tries = 0;
        while (box.intersects(avoid) && tries < 40) {
          if (def.id == "mouse_hint") {
            box.translate(12.0, 0.0);
            if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
          } else {
            box.translate(-12.0, 0.0);
            if (box.left() < 10) box.moveLeft(10);
          }
          ++tries;
        }
      }
    }

    // Prevent explanation boxes from overlapping each other.
    auto overlaps_existing = [&](const QRectF &candidate) {
      for (const QRectF &placed : placed_boxes) {
        if (step == 4 || step == 5 || step == 6) {
          const QRectF c = candidate.adjusted(-10.0, -8.0, 10.0, 8.0);
          const QRectF p = placed.adjusted(-10.0, -8.0, 10.0, 8.0);
          if (c.intersects(p)) return true;
        } else {
          if (candidate.intersects(placed)) return true;
        }
      }
      return false;
    };
    const bool pin_box_position = (step == 5 && def.id == "sig_bdt_gpst");
    if (!pin_box_position && overlaps_existing(box)) {
      QPointF sep_dir = box.center() - ray_rect.center();
      const double sep_len = std::hypot(sep_dir.x(), sep_dir.y());
      if (sep_len > 1e-6) {
        sep_dir = QPointF(sep_dir.x() / sep_len, sep_dir.y() / sep_len);
      } else {
        sep_dir = QPointF(1.0, 0.0);
      }

      int iter = 0;
      while (overlaps_existing(box) && iter < 80) {
        // Primary separation along radial direction, secondary slight tangential drift.
        const QPointF tangential(-sep_dir.y(), sep_dir.x());
        const double tan_step = ((iter % 2) == 0 ? 1.0 : -1.0) *
          (step == 2 ? 3.0 : ((step == 5 || step == 6 || step == 7 || step == 8) ? 4.0 : 2.0));
        box.translate(sep_dir.x() * 9.0 + tangential.x() * tan_step,
                      sep_dir.y() * 9.0 + tangential.y() * tan_step);

        if (box.left() < 10) box.moveLeft(10);
        if (box.top() < 10) box.moveTop(10);
        if (box.right() > in.win_width - 10) box.moveRight(in.win_width - 10);
        if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
        ++iter;
      }
    }

    if (step == 2 && (def.id == "sky_g" || def.id == "sky_c")) {
      const double min_edge_gap = 16.0;
      const double min_center_gap = 120.0;
      const auto keep_inside_view = [&](QRectF *r) {
        if (r->left() < 10) r->moveLeft(10);
        if (r->top() < 10) r->moveTop(10);
        if (r->right() > in.win_width - 10) r->moveRight(in.win_width - 10);
        if (r->bottom() > in.win_height - 58) r->moveBottom(in.win_height - 58);
      };
      const auto too_close_to = [&](const QRectF &a, const QRectF &b) {
        const QRectF b_with_gap = b.adjusted(-min_edge_gap, -min_edge_gap,
                                             min_edge_gap, min_edge_gap);
        if (a.intersects(b_with_gap)) return true;
        const QPointF d = a.center() - b.center();
        return std::hypot(d.x(), d.y()) < min_center_gap;
      };
      const auto push_away = [&](QRectF *moving, const QRectF &fixed,
                                 const QPointF &dir_hint) {
        QPointF dir = dir_hint;
        const double len = std::hypot(dir.x(), dir.y());
        if (len > 1e-6) {
          dir = QPointF(dir.x() / len, dir.y() / len);
        } else {
          dir = QPointF(1.0, 0.0);
        }
        int guard = 0;
        while (too_close_to(*moving, fixed) && guard < 80) {
          moving->translate(dir.x() * 8.0, dir.y() * 8.0);
          keep_inside_view(moving);
          ++guard;
        }
      };

      if (def.id == "sky_g" && has_sky_c_box) {
        const QPointF hint = box.center().y() <= sky_c_box.center().y()
                                 ? QPointF(1.0, -0.8)
                                 : QPointF(1.0, 0.8);
        push_away(&box, sky_c_box, hint);
      }
      if (def.id == "sky_c" && has_sky_g_box) {
        const QPointF hint = box.center().y() >= sky_g_box.center().y()
                                 ? QPointF(1.0, 0.8)
                                 : QPointF(1.0, -0.8);
        push_away(&box, sky_g_box, hint);
      }
    }

    QPointF anchor = rect_edge_anchor_toward(ray_rect, box.center());
    if ((step == 1 || step == 2 || step == 3 || step == 4 || step == 5 || step == 6 || step == 7 || step == 8) &&
        use_anchor_override) {
      anchor = anchor_override;
    }

    if (step == 1) {
      const QPointF target = box.center();
      const QPointF v = target - anchor;
      const double len = std::max(1e-6, std::hypot(v.x(), v.y()));
      const QPointF n(-v.y() / len, v.x() / len);
      const double bend = std::min(24.0, len * 0.08);
      const QPointF ctrl = (anchor + target) * 0.5 + n * bend;

      QPainterPath path(anchor);
      path.quadTo(ctrl, target);

      const int glow_alpha = 55 + (int)std::lround(32.0 * pulse);
      const int core_alpha = 188 + (int)std::lround(52.0 * pulse);
      p.setPen(QPen(QColor(56, 189, 248, glow_alpha), 4.0, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(path);

      p.setPen(QPen(QColor(125, 211, 252, core_alpha), 2.0, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(path);

      p.setPen(Qt::NoPen);
      p.setBrush(QColor(56, 189, 248, 170));
      p.drawEllipse(anchor, 5.2, 5.2);
      p.setBrush(QColor(229, 238, 252, 235));
      p.drawEllipse(anchor, 2.5, 2.5);
      p.setBrush(QColor(56, 189, 248, 190));
      p.drawEllipse(target, 4.6, 4.6);
      p.setBrush(QColor(229, 238, 252, 235));
      p.drawEllipse(target, 2.1, 2.1);
    } else {
      const QPointF target = box.center();
      QPainterPath path(anchor);
      path.lineTo(target);

      const int glow_alpha = 55 + (int)std::lround(28.0 * pulse);
      const int core_alpha = 188 + (int)std::lround(48.0 * pulse);
      p.setPen(QPen(QColor(56, 189, 248, glow_alpha), 3.8, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(path);
      p.setPen(QPen(QColor(125, 211, 252, core_alpha), 2.0, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(path);

      p.setPen(Qt::NoPen);
      p.setBrush(QColor(56, 189, 248, 170));
      p.drawEllipse(anchor, 5.2, 5.2);
      p.setBrush(QColor(229, 238, 252, 235));
      p.drawEllipse(anchor, 2.5, 2.5);
      p.setBrush(QColor(56, 189, 248, 190));
      p.drawEllipse(target, 4.6, 4.6);
      p.setBrush(QColor(229, 238, 252, 235));
      p.drawEllipse(target, 2.1, 2.1);
    }

    {
      // Pre-measure body text to ensure the box is tall enough.
      int nl_idx = def.text.indexOf('\n');
      if (nl_idx >= 0) {
        const QString preBody = def.text.mid(nl_idx + 1);
        QFont bf = text_font;
        bf.setPointSize(std::max(8, text_font.pointSize() - 1));
        QFontMetrics bfm(bf);
        const int avail_w = std::max(60, (int)box.width() - 20);
        const QRect needed = bfm.boundingRect(
            QRect(0, 0, avail_w, 4000),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, preBody);
        // 5 (top pad) + 22 (title) + 2 (gap) + body + 5 (bot pad)
        const double min_h = 34.0 + needed.height() + 5.0;
        if (box.height() < min_h) {
          box.setHeight(min_h);
          // Re-clamp to screen
          if (box.bottom() > in.win_height - 58) box.moveBottom(in.win_height - 58);
          if (box.top() < 10) box.moveTop(10);
        }
      }
    }

    p.setPen(QPen(QColor(186, 230, 253, 210), 1));
    p.setBrush(QColor(8, 20, 38, 236));
    p.drawRoundedRect(box, 9, 9);

    {
      // Split title (first line) and body (rest) for proper layered rendering
      int nl_idx = def.text.indexOf('\n');
      const QString cTitle = (nl_idx >= 0) ? def.text.left(nl_idx) : def.text;
      const QString cBody  = (nl_idx >= 0) ? def.text.mid(nl_idx + 1) : QString();
      const QRectF inner = box.adjusted(10, 5, -10, -5);
      if (cBody.isEmpty()) {
        // Single-line: draw centred as before
        p.setPen(QColor("#e5eefc"));
        p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, cTitle);
      } else {
        // Title (bold, accent blue) — top 22 px of inner area
        QFont tf = text_font;
        p.setFont(tf);
        p.setPen(QColor(125, 211, 252));
        p.drawText(QRectF(inner.x(), inner.y(), inner.width(), 22),
                   Qt::AlignLeft | Qt::AlignVCenter, cTitle);
        // Body (regular, slightly smaller) — below the title
        QFont bf = text_font;
        bf.setPointSize(std::max(8, text_font.pointSize() - 1));
        p.setFont(bf);
        p.setPen(QColor(200, 220, 245));
        p.drawText(QRectF(inner.x(), inner.y() + 24, inner.width(), inner.height() - 24),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, cBody);
        p.setFont(text_font);
      }
    }

    if (step == 3 && def.id == "sky_g") {
      sky_g_box = box;
      has_sky_g_box = true;
    } else if (step == 3 && def.id == "sky_c") {
      sky_c_box = box;
      has_sky_c_box = true;
    }

    placed_boxes.push_back(box);
    state->callout_hit_boxes.push_back(box);
    state->callout_hit_anchors.push_back(anchor);
  }

  // ── Glow animation on clicked callout anchor ──────────────────────────────
  if (state->has_glow && state->glow_step == step) {
    const double ge = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state->glow_start_tp).count();
    const QPointF ga = state->glow_anchor;
    const double kPeriod = 1.4;
    p.setBrush(Qt::NoBrush);
    for (int ring = 0; ring < 2; ++ring) {
      const double phase = std::fmod(ge + ring * kPeriod * 0.5, kPeriod) / kPeriod;
      const double ring_r = 7.0 + phase * 32.0;
      const int ring_a = (int)(220.0 * (1.0 - phase));
      p.setPen(QPen(QColor(80, 240, 160, ring_a), 2.0));
      p.drawEllipse(ga, ring_r, ring_r);
    }
    const double sp = 0.65 + 0.35 * std::sin(ge * 3.14159265 * 2.2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(80, 240, 160, (int)(200 * sp)));
    p.drawEllipse(ga, 5.0, 5.0);
    p.setBrush(QColor(255, 255, 255, (int)(220 * sp)));
    p.drawEllipse(ga, 2.5, 2.5);
  }

  QFont title_font = text_font;
  title_font.setPointSize(text_font.pointSize() + 1);
  p.setFont(title_font);
  p.setPen(QColor("#7dd3fc"));
  p.drawText(QRect(16, 10, std::max(200, in.win_width - 260), 28),
             Qt::AlignLeft | Qt::AlignVCenter,
               gui_i18n_text(in.language, "tutorial.overlay.part_title")
                 .arg(step)
                 .arg(in.last_step)
                 .arg(tutorial_step_title(step, in.language)));

  auto draw_overlay_btn = [&](const QRect &r, const QColor &fill,
                              const QColor &text, const char *label) {
    Rect rr{r.x(), r.y(), r.width(), r.height()};
    control_draw_button_filled(p, rr, fill, fill, text, label);
  };

  // Draw buttons in left-to-right order: CONTENTS | PREV | NEXT | EXIT
  {
    const QByteArray lbl = gui_i18n_text(in.language, "tutorial.btn.contents").toUtf8();
    const QFont saved_btn_font = p.font();
    QFont contents_font = saved_btn_font;
    contents_font.setPointSize(std::max(8, saved_btn_font.pointSize() - 2));
    p.setFont(contents_font);
    draw_overlay_btn(state->contents_btn_rect, QColor("#1e3a5f"), QColor("#7dd3fc"),
                     lbl.constData());
    p.setFont(saved_btn_font);
  }
  {
    const QByteArray lbl = gui_i18n_text(in.language, "tutorial.btn.prev").toUtf8();
    draw_overlay_btn(state->prev_btn_rect, QColor("#334155"), QColor("#f8fbff"),
                     lbl.constData());
  }
  if (step < in.last_step) {
    const QByteArray lbl = gui_i18n_text(in.language, "tutorial.btn.next").toUtf8();
    draw_overlay_btn(state->next_btn_rect, QColor("#0ea5e9"), QColor(8, 12, 18),
                     lbl.constData());
  } else {
    const QByteArray lbl = gui_i18n_text(in.language, "tutorial.btn.done").toUtf8();
    draw_overlay_btn(state->next_btn_rect, QColor("#22c55e"), QColor(8, 12, 18),
                     lbl.constData());
  }
  {
    const QByteArray lbl = gui_i18n_text(in.language, "tutorial.btn.exit").toUtf8();
    draw_overlay_btn(state->close_btn_rect, QColor("#6b7280"), QColor("#f8fbff"),
                     lbl.constData());
  }

  p.setFont(old_font);
  p.restore();

  state->text_page_count = 1;
  if (state->text_page >= 1) state->text_page = 0;
}

TutorialImSceneDef tutorial_im_build_scene(TutorialImSceneId scene_id) {
  TutorialImSceneDef s;
  s.scene_id = scene_id;
  s.center_scale = 1.10f;
  s.move_duration_sec = 0.70f;
  s.mask_alpha = 198;

  if (scene_id == TutorialImSceneId::Map) {
    s.callouts.push_back({"map_top", "Top controls and search", -120.0f, 250.0f, 240.0f, 84.0f});
    s.callouts.push_back({"map_upper", "Upper map status", -45.0f, 250.0f, 220.0f, 80.0f});
    s.callouts.push_back({"map_ll", "Lower-left mission content", 135.0f, 260.0f, 250.0f, 84.0f});
    s.callouts.push_back({"map_lr", "Lower-right telemetry", 45.0f, 260.0f, 240.0f, 84.0f});
  } else if (scene_id == TutorialImSceneId::Skyplot) {
    s.callouts.push_back({"sky_g", "G satellites", -35.0f, 230.0f, 180.0f, 72.0f});
    s.callouts.push_back({"sky_c", "C satellites", 145.0f, 230.0f, 180.0f, 72.0f});
  } else if (scene_id == TutorialImSceneId::SignalSetting) {
    s.callouts.push_back({"sig_simple", "Simple branch", -130.0f, 240.0f, 200.0f, 76.0f});
    s.callouts.push_back({"sig_detail", "Detail branch", 50.0f, 240.0f, 200.0f, 76.0f});
  } else if (scene_id == TutorialImSceneId::Waveform) {
    s.callouts.push_back({"wave_freq", "Frequency attribute", -120.0f, 255.0f, 220.0f, 78.0f});
    s.callouts.push_back({"wave_amp", "Amplitude attribute", 120.0f, 255.0f, 220.0f, 78.0f});
  }

  return s;
}

TutorialImSnapshot tutorial_im_capture_to_fbo_pseudocode(
    const TutorialImRect &src_rect) {
  TutorialImSnapshot out;
  if (src_rect.w <= 1.0f || src_rect.h <= 1.0f) {
    return out;
  }

  // Pseudocode for immediate-mode capture:
  // 1) Create/resize FBO to source size.
  // 2) Bind FBO and set viewport to FBO extent.
  // 3) Render target panel into this FBO only.
  // 4) Unbind FBO and return texture id.
  //
  // Example mapping in an ImGui frame:
  // - renderer.BeginFbo(fbo, src_rect.w, src_rect.h)
  // - RenderMapPanelOnly() / RenderSignalPanelOnly() / RenderWaveformOnly()
  // - renderer.EndFbo()
  // - out.texture_id = renderer.GetFboTexture(fbo)

  out.fbo_handle = 1;
  out.texture_id = 1;
  out.tex_w = (int)std::lround(src_rect.w);
  out.tex_h = (int)std::lround(src_rect.h);
  out.valid = true;
  return out;
}

void tutorial_im_on_step_changed(TutorialImRuntime *rt, int new_step,
                                 std::chrono::steady_clock::time_point now_tp) {
  if (!rt) return;
  if (rt->step_index == new_step) return;

  rt->step_index = new_step;
  rt->anim_start_tp = now_tp;
  rt->focus_center_window_once = true;
}

std::vector<TutorialImVec2> tutorial_im_calculate_radial_positions(
    const TutorialImVec2 &center,
    const std::vector<TutorialImCalloutDef> &callouts,
    float radius_scale) {
  std::vector<TutorialImVec2> out;
  out.reserve(callouts.size());
  const float rs = std::max(0.1f, radius_scale);

  for (const TutorialImCalloutDef &c : callouts) {
    const double theta = (double)c.angle_deg * 3.14159265358979323846 / 180.0;
    const float r = std::max(24.0f, c.radius_px * rs);
    TutorialImVec2 p;
    p.x = center.x + (float)std::cos(theta) * r;
    p.y = center.y + (float)std::sin(theta) * r;
    out.push_back(p);
  }
  return out;
}

TutorialGalaxyGuideStepConfig BuildGuideStepForComponent(
    const std::string &component_id, int index, int total, float fallback_radius) {
  struct Mapping {
    const char *id;
    const char *text;
    float angle;
    float radius;
  };

  static const Mapping kMap[] = {
      {"map_top", "Top controls: quick search and map actions.", 300.0f, 290.0f},
      {"map_upper", "Upper map status: zoom level and viewport context.", 345.0f, 300.0f},
      {"map_ll", "Lower-left panel: mission route and task summary.", 120.0f, 315.0f},
      {"map_lr", "Lower-right panel: telemetry and short health metrics.", 55.0f, 315.0f},
      {"sky_g", "G satellites: GPS constellation traces and points.", 330.0f, 285.0f},
      {"sky_c", "C satellites: BDS constellation traces and points.", 25.0f, 285.0f},
      {"sig_simple", "Simple page: preset controls for rapid setup.", 210.0f, 300.0f},
      {"sig_detail", "Detail page: parameter-level tuning controls.", 330.0f, 300.0f},
      {"wave_freq", "Frequency attribute: center frequency and drift.", 220.0f, 310.0f},
      {"wave_amp", "Amplitude attribute: level envelope and peaks.", 330.0f, 310.0f},
      {"sdr_a_btn", "SDR A Button: switch to SDR A control context.", 0.0f, 300.0f},
        {"sdr_b_btn", "SDR B Button: switch to SDR B control context.", 36.0f, 300.0f},
        {"btn_tab_simple", "Simple Tab: switch control panel to simple mode.", 210.0f, 300.0f},
        {"btn_tab_detail", "Detail Tab: switch control panel to detail mode.", 330.0f, 300.0f},
      {"frequency_input", "Frequency Input: set RF center frequency.", 72.0f, 300.0f},
      {"gain_slider", "Gain Slider: adjust receiver gain level.", 144.0f, 300.0f},
      {"sample_rate", "Sample Rate: configure ADC/DSP sampling.", 216.0f, 300.0f},
        {"format_switch", "Format Switch: toggle output sample format.", 252.0f, 300.0f},
      {"start_button", "Start Button: launch current simulation pipeline.", 288.0f, 300.0f},
        {"stop_button", "Stop Button: stop current simulation safely.", 324.0f, 300.0f},
        {"exit_button", "Exit Button: leave current simulation session.", 20.0f, 300.0f},
  };

  for (const Mapping &m : kMap) {
    if (component_id == m.id) {
      TutorialGalaxyGuideStepConfig cfg;
      cfg.anchor_component_id = component_id;
      cfg.text = m.text;
      cfg.angle_deg = m.angle;
      cfg.radius_px = m.radius;
      return cfg;
    }
  }

  const int safe_total = std::max(1, total);
  const float evenly_spaced_angle = 360.0f * (float)index / (float)safe_total;
  TutorialGalaxyGuideStepConfig fallback;
  fallback.anchor_component_id = component_id;
  fallback.text = component_id + ": highlighted component.";
  fallback.angle_deg = evenly_spaced_angle;
  fallback.radius_px = std::max(220.0f, fallback_radius);
  return fallback;
}

bool HasAnchorId(const SunWindowData &central_window, const std::string &component_id) {
  for (const TutorialSunAnchorPoint &a : central_window.anchor_buffer) {
    if (a.valid && a.component_id == component_id) return true;
  }
  return false;
}

void AddFallbackAnchorIfMissing(SunWindowData *central_window,
                                const std::string &component_id,
                                float nx, float ny) {
  if (!central_window) return;
  if (HasAnchorId(*central_window, component_id)) return;

  TutorialSunAnchorPoint a;
  a.component_id = component_id;
  a.abs_pos.x = central_window->rect.x + central_window->rect.w * nx;
  a.abs_pos.y = central_window->rect.y + central_window->rect.h * ny;
  a.valid = true;
  central_window->anchor_buffer.push_back(a);
}

void InjectGuideButtonAnchorsByScene(SunWindowData *central_window,
                                     TutorialImSceneId scene_id) {
  if (!central_window) return;

  if (scene_id == TutorialImSceneId::Map) {
    AddFallbackAnchorIfMissing(central_window, "sdr_a_btn", 0.20f, 0.12f);
    AddFallbackAnchorIfMissing(central_window, "sdr_b_btn", 0.34f, 0.12f);
    AddFallbackAnchorIfMissing(central_window, "start_button", 0.82f, 0.88f);
    AddFallbackAnchorIfMissing(central_window, "stop_button", 0.66f, 0.88f);
  } else if (scene_id == TutorialImSceneId::SignalSetting) {
    AddFallbackAnchorIfMissing(central_window, "btn_tab_simple", 0.24f, 0.10f);
    AddFallbackAnchorIfMissing(central_window, "btn_tab_detail", 0.40f, 0.10f);
    AddFallbackAnchorIfMissing(central_window, "frequency_input", 0.56f, 0.32f);
    AddFallbackAnchorIfMissing(central_window, "gain_slider", 0.56f, 0.46f);
    AddFallbackAnchorIfMissing(central_window, "sample_rate", 0.56f, 0.60f);
    AddFallbackAnchorIfMissing(central_window, "format_switch", 0.56f, 0.74f);
    AddFallbackAnchorIfMissing(central_window, "start_button", 0.80f, 0.90f);
    AddFallbackAnchorIfMissing(central_window, "stop_button", 0.64f, 0.90f);
    AddFallbackAnchorIfMissing(central_window, "exit_button", 0.15f, 0.90f);
  } else if (scene_id == TutorialImSceneId::Waveform) {
    AddFallbackAnchorIfMissing(central_window, "frequency_input", 0.22f, 0.14f);
    AddFallbackAnchorIfMissing(central_window, "format_switch", 0.44f, 0.14f);
    AddFallbackAnchorIfMissing(central_window, "start_button", 0.82f, 0.88f);
    AddFallbackAnchorIfMissing(central_window, "stop_button", 0.66f, 0.88f);
  } else if (scene_id == TutorialImSceneId::Skyplot) {
    AddFallbackAnchorIfMissing(central_window, "start_button", 0.82f, 0.88f);
    AddFallbackAnchorIfMissing(central_window, "stop_button", 0.66f, 0.88f);
  }
}

void DrawGalaxyCallouts(const SunWindowData &central_window) {
#if TUTORIAL_HAS_IMGUI
  if (central_window.rect.w <= 1.0f || central_window.rect.h <= 1.0f) return;
  if (central_window.guide_steps.empty()) return;

  ImDrawList *draw_list = ImGui::GetForegroundDrawList();
  if (!draw_list) return;

  const TutorialImVec2 c = {
      central_window.rect.x + central_window.rect.w * 0.5f,
      central_window.rect.y + central_window.rect.h * 0.5f,
  };

  const auto find_anchor = [&](const std::string &component_id,
                               TutorialImVec2 *out_anchor) -> bool {
    if (!out_anchor) return false;
    for (const TutorialSunAnchorPoint &a : central_window.anchor_buffer) {
      if (a.valid && a.component_id == component_id) {
        *out_anchor = a.abs_pos;
        return true;
      }
    }
    return false;
  };

  const ImU32 line_color = ImColor(100, 200, 255, 150);
  const ImU32 dot_color = ImColor(170, 230, 255, 220);
  const ImU32 box_color = ImColor(8, 24, 42, 228);
  const ImU32 box_border = ImColor(125, 211, 252, 195);
  const ImU32 text_color = ImColor(229, 238, 252, 255);

  for (const TutorialGalaxyGuideStepConfig &step : central_window.guide_steps) {
    const float radius =
        std::max(24.0f, step.radius_px > 0.0f ? step.radius_px
                                               : central_window.default_radius_px);
    const float angle_deg = step.angle_deg + central_window.orbit_spin_deg;
    const float theta = angle_deg * 3.14159265358979323846f / 180.0f;

    const TutorialImVec2 planet = {
        c.x + std::cos(theta) * radius,
        c.y + std::sin(theta) * radius,
    };

    TutorialImVec2 anchor = c;
    if (!find_anchor(step.anchor_component_id, &anchor)) {
      const float hw = std::max(1.0f, central_window.rect.w * 0.5f);
      const float hh = std::max(1.0f, central_window.rect.h * 0.5f);
      const float dx = planet.x - c.x;
      const float dy = planet.y - c.y;
      const float tx = std::abs(dx) < 1e-5f ? 1e6f : hw / std::abs(dx);
      const float ty = std::abs(dy) < 1e-5f ? 1e6f : hh / std::abs(dy);
      const float k = std::min(tx, ty);
      anchor = {c.x + dx * k, c.y + dy * k};
    }

    draw_list->AddLine(ImVec2(anchor.x, anchor.y), ImVec2(planet.x, planet.y),
                       line_color, 2.0f);
    draw_list->AddCircleFilled(ImVec2(anchor.x, anchor.y), 3.5f, dot_color, 16);
    draw_list->AddCircleFilled(ImVec2(planet.x, planet.y), 4.0f, dot_color, 16);

    const ImVec2 text_size =
        ImGui::CalcTextSize(step.text.c_str(), nullptr, false, 260.0f);
    const float pad_x = 10.0f;
    const float pad_y = 8.0f;
    const ImVec2 box_min(planet.x + 10.0f, planet.y - text_size.y * 0.5f - pad_y);
    const ImVec2 box_max(box_min.x + text_size.x + pad_x * 2.0f,
                         box_min.y + text_size.y + pad_y * 2.0f);

    draw_list->AddRectFilled(box_min, box_max, box_color, 8.0f);
    draw_list->AddRect(box_min, box_max, box_border, 8.0f, 0, 1.0f);
    draw_list->AddText(ImVec2(box_min.x + pad_x, box_min.y + pad_y), text_color,
                       step.text.c_str());
  }
#else
  (void)central_window;
#endif
}

void RenderRadialCallouts(TutorialImFrameLayout *layout,
                          const TutorialImRect &center_rect) {
  if (!layout) return;
  const TutorialImVec2 center = {center_rect.x + center_rect.w * 0.5f,
                                 center_rect.y + center_rect.h * 0.5f};

  std::vector<TutorialImCalloutDef> defs;
  defs.reserve(layout->callouts.size());
  for (const TutorialImCalloutLayout &c : layout->callouts) {
    defs.push_back(c.def);
  }
  const std::vector<TutorialImVec2> radial =
      tutorial_im_calculate_radial_positions(center, defs, 1.0f);

  for (int i = 0; i < (int)layout->callouts.size() && i < (int)radial.size(); ++i) {
    TutorialImCalloutLayout &c = layout->callouts[i];
    c.center = radial[i];
    c.box = {c.center.x - c.def.box_w * 0.5f, c.center.y - c.def.box_h * 0.5f,
             c.def.box_w, c.def.box_h};

    const float dx = c.center.x - center.x;
    const float dy = c.center.y - center.y;
    const float hw = std::max(1.0f, center_rect.w * 0.5f);
    const float hh = std::max(1.0f, center_rect.h * 0.5f);
    const float tx = std::abs(dx) < 1e-5f ? 1e6f : hw / std::abs(dx);
    const float ty = std::abs(dy) < 1e-5f ? 1e6f : hh / std::abs(dy);
    const float k = std::min(tx, ty);
    c.edge_anchor = {center.x + dx * k, center.y + dy * k};
  }

  // Pseudocode draw path in ImGui:
  // ImDrawList* dl = ImGui::GetForegroundDrawList();
  // for each callout in layout->callouts:
  //   dl->AddLine(edge_anchor, callout_center, line_color, 2.0f);
  //   dl->AddRectFilled(box_min, box_max, box_color, 8.0f);
  //   dl->AddText(text_pos, text_color, callout_text);
}

TutorialImFrameLayout tutorial_im_build_frame_layout(
    const TutorialImRuntime &rt, float screen_w, float screen_h,
    std::chrono::steady_clock::time_point now_tp) {
  TutorialImFrameLayout out;
  if (!rt.active || !rt.snapshot.valid || screen_w <= 1.0f || screen_h <= 1.0f) {
    return out;
  }

  const float elapsed =
      (float)std::chrono::duration<double>(now_tp - rt.anim_start_tp).count();
  const float duration = std::max(0.05f, rt.scene.move_duration_sec);
  float t = elapsed / duration;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  out.progress = t;

  const TutorialImRect src = rt.scene.source_rect;
  const float dst_w = std::max(40.0f, src.w * std::max(1.0f, rt.scene.center_scale));
  const float dst_h = std::max(30.0f, src.h * std::max(1.0f, rt.scene.center_scale));
  const TutorialImRect dst = {(screen_w - dst_w) * 0.5f, (screen_h - dst_h) * 0.5f,
                              dst_w, dst_h};

  out.current_rect.x = src.x + (dst.x - src.x) * t;
  out.current_rect.y = src.y + (dst.y - src.y) * t;
  out.current_rect.w = src.w + (dst.w - src.w) * t;
  out.current_rect.h = src.h + (dst.h - src.h) * t;

  const TutorialImVec2 center = {out.current_rect.x + out.current_rect.w * 0.5f,
                                 out.current_rect.y + out.current_rect.h * 0.5f};
  const std::vector<TutorialImVec2> radial =
      tutorial_im_calculate_radial_positions(center, rt.scene.callouts, 1.0f);

  for (int i = 0; i < (int)rt.scene.callouts.size() && i < (int)radial.size(); ++i) {
    TutorialImCalloutLayout item;
    item.def = rt.scene.callouts[i];
    item.center = radial[i];
    item.box = {item.center.x - item.def.box_w * 0.5f,
                item.center.y - item.def.box_h * 0.5f,
                item.def.box_w, item.def.box_h};

    const TutorialImVec2 c = center;
    const float dx = item.center.x - c.x;
    const float dy = item.center.y - c.y;
    const float hw = std::max(1.0f, out.current_rect.w * 0.5f);
    const float hh = std::max(1.0f, out.current_rect.h * 0.5f);
    const float tx = std::abs(dx) < 1e-5f ? 1e6f : hw / std::abs(dx);
    const float ty = std::abs(dy) < 1e-5f ? 1e6f : hh / std::abs(dy);
    const float k = std::min(tx, ty);
    item.edge_anchor = {c.x + dx * k, c.y + dy * k};
    out.callouts.push_back(item);
  }

  return out;
}

void tutorial_im_render_overlay_pseudocode(TutorialImRuntime *rt,
                                           float screen_w,
                                           float screen_h) {
  if (!rt || !rt->active || !rt->snapshot.valid) return;

  const auto now_tp = std::chrono::steady_clock::now();
  TutorialImFrameLayout frame =
      tutorial_im_build_frame_layout(*rt, screen_w, screen_h, now_tp);
  if (frame.current_rect.w <= 0.0f || frame.current_rect.h <= 0.0f) return;

#if TUTORIAL_HAS_IMGUI
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(screen_w, screen_h));
  ImGui::Begin(rt->background_window_name.c_str(), nullptr,
               ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);

  ImDrawList *fg = ImGui::GetForegroundDrawList();
  fg->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screen_w, screen_h),
                    IM_COL32(6, 10, 18, rt->scene.mask_alpha));
  ImGui::InvisibleButton("##tutorial_bg_blocker", ImVec2(screen_w, screen_h));
  ImGui::End();

  // Force center tutorial window as top-most viewport window before Begin.
  ImGuiWindowClass window_class;
  window_class.ViewportFlagsOverrideSet = ImGuiViewportFlags_TopMost;
  ImGui::SetNextWindowClass(&window_class);

  if (rt->focus_center_window_once || rt->focused_step_index != rt->step_index) {
    ImGui::SetNextWindowFocus();
  }

  ImGui::SetNextWindowPos(ImVec2(frame.current_rect.x, frame.current_rect.y));
  ImGui::SetNextWindowSize(ImVec2(frame.current_rect.w, frame.current_rect.h));
  ImGui::Begin(rt->center_window_name.c_str(), nullptr,
               ImGuiWindowFlags_NoDecoration |
                   ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);

  fg->AddImage((ImTextureID)(uintptr_t)rt->snapshot.texture_id,
               ImVec2(frame.current_rect.x, frame.current_rect.y),
               ImVec2(frame.current_rect.x + frame.current_rect.w,
                      frame.current_rect.y + frame.current_rect.h));
  ImGui::End();

  rt->focus_center_window_once = false;
  rt->focused_step_index = rt->step_index;

  RenderRadialCallouts(&frame, frame.current_rect);
  SunWindowData sun_window;
  sun_window.rect = frame.current_rect;
  sun_window.default_radius_px = 300.0f;
  sun_window.orbit_spin_deg = 0.0f;

  for (const auto &c : frame.callouts) {
    TutorialSunAnchorPoint anchor;
    anchor.component_id = c.def.id;
    anchor.abs_pos = c.edge_anchor;
    anchor.valid = true;
    sun_window.anchor_buffer.push_back(anchor);
  }

  // Ensure all guide-related button components also explode from center.
  InjectGuideButtonAnchorsByScene(&sun_window, rt->scene.scene_id);

  for (int i = 0; i < (int)sun_window.anchor_buffer.size(); ++i) {
    const TutorialSunAnchorPoint &a = sun_window.anchor_buffer[i];
    TutorialGalaxyGuideStepConfig step_cfg = BuildGuideStepForComponent(
        a.component_id, i, (int)sun_window.anchor_buffer.size(),
        sun_window.default_radius_px);
    sun_window.guide_steps.push_back(step_cfg);
  }

  DrawGalaxyCallouts(sun_window);
#else
  (void)frame;
  (void)screen_w;
  (void)screen_h;
#endif
}