#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

#include "gui/core/i18n/gui_font_manager.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/layout/geometry/control_layout.h"
#include "gui/layout/geometry/quad_panel_layout.h"
#include "gui/geo/geo_io.h"
#include "gui/tutorial/flow/tutorial_flow_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_callout_rules_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_callout_defs_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_layout_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_math_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_render_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_toc_utils.h"

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

} // namespace

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

  const int step = std::max(0, std::min(in.step, in.last_step));
  TutorialOverlayInput focus_in = in;
  focus_in.step = step;
  QRect focus = tutorial_focus_rect_for_step(focus_in);

  if (focus.isEmpty() && step != 0) {
    const int w = std::max(220, in.win_width / 3);
    const int h = std::max(160, in.win_height / 4);
    focus = QRect((in.win_width - w) / 2, (in.win_height - h) / 2, w, h);
  }

  if (tutorial_draw_toc_overlay(p, in, state, &s_capture_in_progress,
                                &s_session_frozen, &s_frozen_host,
                                &s_frozen_size, &s_frozen_background)) {
    return;
  }

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
    callouts = tutorial_overlay_build_step_callouts(in, step);
  }

  tutorial_overlay_scale_guide_callouts(&callouts);

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

  TutorialOverlayAnchorRects rule_anchors;
  rule_anchors.search_anchor_rect = search_anchor_rect;
  rule_anchors.nfz_anchor_rect = nfz_anchor_rect;
  rule_anchors.dark_anchor_rect = dark_anchor_rect;
  rule_anchors.guide_anchor_rect = guide_anchor_rect;
  rule_anchors.lang_anchor_rect = lang_anchor_rect;
  rule_anchors.osm_llh_anchor_rect = osm_llh_anchor_rect;
  rule_anchors.nfz_restricted_anchor_rect = nfz_restricted_anchor_rect;
  rule_anchors.osm_runtime_anchor_rect = osm_runtime_anchor_rect;
  rule_anchors.osm_stop_anchor_rect = osm_stop_anchor_rect;
  rule_anchors.wave_1_anchor_rect = wave_1_anchor_rect;
  rule_anchors.wave_2_anchor_rect = wave_2_anchor_rect;
  rule_anchors.wave_3_anchor_rect = wave_3_anchor_rect;
  rule_anchors.wave_4_anchor_rect = wave_4_anchor_rect;
  rule_anchors.sig_gear_anchor_rect = sig_gear_anchor_rect;
  rule_anchors.sig_utc_anchor_rect = sig_utc_anchor_rect;
  rule_anchors.sig_bdt_anchor_rect = sig_bdt_anchor_rect;
  rule_anchors.sig_gpst_anchor_rect = sig_gpst_anchor_rect;
  rule_anchors.sig_tab_simple_anchor_rect = sig_tab_simple_anchor_rect;
  rule_anchors.sig_tab_detail_anchor_rect = sig_tab_detail_anchor_rect;
  rule_anchors.sig_interfere_anchor_rect = sig_interfere_anchor_rect;
  rule_anchors.sig_system_anchor_rect = sig_system_anchor_rect;
  rule_anchors.sig_fs_anchor_rect = sig_fs_anchor_rect;
  rule_anchors.sig_tx_anchor_rect = sig_tx_anchor_rect;
  rule_anchors.sig_start_anchor_rect = sig_start_anchor_rect;
  rule_anchors.sig_exit_anchor_rect = sig_exit_anchor_rect;
  rule_anchors.detail_sats_anchor_rect = detail_sats_anchor_rect;
  rule_anchors.gain_slider_anchor_rect = gain_slider_anchor_rect;
  rule_anchors.cn0_slider_anchor_rect = cn0_slider_anchor_rect;
  rule_anchors.path_v_slider_anchor_rect = path_v_slider_anchor_rect;
  rule_anchors.path_a_slider_anchor_rect = path_a_slider_anchor_rect;
  rule_anchors.prn_slider_anchor_rect = prn_slider_anchor_rect;
  rule_anchors.ch_slider_anchor_rect = ch_slider_anchor_rect;
  rule_anchors.sw_fmt_anchor_rect = sw_fmt_anchor_rect;
  rule_anchors.sw_mode_anchor_rect = sw_mode_anchor_rect;
  rule_anchors.tg_meo_anchor_rect = tg_meo_anchor_rect;
  rule_anchors.tg_iono_anchor_rect = tg_iono_anchor_rect;
  rule_anchors.tg_clk_anchor_rect = tg_clk_anchor_rect;
  rule_anchors.step6_two_column_layout = step6_two_column_layout;

  TutorialOverlayFrameContext rule_ctx;
  rule_ctx.ray_rect = ray_rect;
  rule_ctx.win_width = in.win_width;
  rule_ctx.win_height = in.win_height;
  rule_ctx.step = step;

  const auto step0_anchor_from_id = [&](const QString &id) -> QPointF {
    return tutorial_overlay_step1_anchor_from_id(id, rule_ctx, rule_anchors,
                                                 map_anchor_to_ray_rect);
  };

  const auto step1_anchor_from_id = [&](const QString &id) -> QPointF {
    return tutorial_overlay_step2_anchor_from_id(id, rule_ctx, rule_anchors,
                                                 map_anchor_to_ray_rect);
  };

  const auto step3_anchor_from_id = [&](const QString &id) -> QPointF {
    return tutorial_overlay_step3_anchor_from_id(id, rule_ctx, rule_anchors,
                                                 map_anchor_to_ray_rect);
  };

  const auto step4_anchor_from_id = [&](const QString &id) -> QPointF {
    return tutorial_overlay_step4_anchor_from_id(id, rule_ctx, rule_anchors,
                                                 map_anchor_to_ray_rect);
  };

  int slot_left = 0;
  int slot_right = 0;
  int slot_top = 0;
  int slot_bottom = 0;

  const auto step0_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    return tutorial_overlay_step1_target_from_anchor(
        id, anchor, box_size, rule_ctx, rule_anchors, map_anchor_to_ray_rect,
        &slot_left, &slot_right, &slot_top, &slot_bottom);
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
    return tutorial_overlay_step4_target_from_anchor(id, anchor, box_size,
                                                     rule_ctx);
  };

  const auto step4_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    return tutorial_overlay_step5_target_from_anchor(id, anchor, box_size,
                                                     rule_ctx);
  };

  const auto step5_target_from_anchor = [&](const QString &id,
                                             const QSize &box_size) -> QPointF {
    return tutorial_overlay_step6_target_from_anchor(id, box_size,
                                                     rule_ctx);
  };

  const auto step6_anchor_from_id = [&](const QString &id) -> QPointF {
    return tutorial_overlay_step7_anchor_from_id(id, rule_ctx, rule_anchors,
                                                 map_anchor_to_ray_rect);
  };

  const auto step6_target_from_anchor = [&](const QString &id,
                                             const QPointF &anchor,
                                             const QSize &box_size) -> QPointF {
    return tutorial_overlay_step7_target_from_anchor(id, anchor, box_size,
                                                     rule_ctx, rule_anchors);
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
      c = step5_target_from_anchor(def.id, def.box_size);
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
      c = step5_target_from_anchor(def.id, def.box_size);
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
      const QPointF preferred_dir = box.center() - ray_rect.center();
      const double theta = def.angle_deg * 3.14159265358979323846 / 180.0;
      const QPointF fallback_dir(std::cos(theta), std::sin(theta));
      tutorial_overlay_push_box_outside_avoid(
          &box, avoid, preferred_dir, fallback_dir, 10.0, 48, in.win_width,
          in.win_height);
      tutorial_overlay_clamp_callout_box(&box, in.win_width, in.win_height);
    }

    if (step == 3 && (def.id == "sky_g" || def.id == "sky_c")) {
      // Keep G/C explanation cards outside the guide carrier body.
      const QRectF avoid = ray_rect.adjusted(-10.0, -10.0, 10.0, 10.0);
      const QPointF preferred_dir = box.center() - ray_rect.center();
      const QPointF fallback_dir =
          (def.id == "sky_g") ? QPointF(1.0, -0.2) : QPointF(1.0, 0.2);
      tutorial_overlay_push_box_outside_avoid(
          &box, avoid, preferred_dir, fallback_dir, 10.0, 56, in.win_width,
          in.win_height);
      tutorial_overlay_clamp_callout_box(&box, in.win_width, in.win_height);
    }

    if (step == 2) {
      tutorial_overlay_apply_step2_special_avoidance(
          def.id, ray_rect, &box, in.win_width, in.win_height);
    }

    const bool pin_box_position = (step == 5 && def.id == "sig_bdt_gpst");
    if (!pin_box_position) {
      tutorial_overlay_resolve_overlap_with_placed(
          &box, placed_boxes, ray_rect, step, in.win_width, in.win_height);
    }

    if (step == 2 && (def.id == "sky_g" || def.id == "sky_c")) {
      const bool has_other_box = (def.id == "sky_g") ? has_sky_c_box : has_sky_g_box;
      const QRectF other_box = (def.id == "sky_g") ? sky_c_box : sky_g_box;
      tutorial_overlay_apply_step2_sky_pair_spacing(
          def.id, &box, in.win_width, in.win_height, has_other_box,
          other_box);
    }

    const QPointF anchor = tutorial_overlay_draw_callout_connector(
        p, ray_rect, box, anchor_override, use_anchor_override, step, pulse);
    tutorial_overlay_adjust_callout_box_height_for_text(
        &box, def, text_font, in.win_height);
    tutorial_overlay_draw_callout_box_and_text(p, box, def, text_font);

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

  tutorial_overlay_draw_nav_buttons(p, in, *state, step);

  p.setFont(old_font);
  p.restore();

  state->text_page_count = 1;
  if (state->text_page >= 1) state->text_page = 0;
}
