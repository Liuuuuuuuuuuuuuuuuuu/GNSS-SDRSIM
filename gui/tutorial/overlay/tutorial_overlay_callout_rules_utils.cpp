#include "gui/tutorial/overlay/tutorial_overlay_callout_rules_utils.h"

#include <algorithm>
#include <cmath>

namespace {

QRect status_text_head(const QRect &r) {
  const int h = std::max(10, std::min(22, r.height() / 2));
  const int w = std::max(26, std::min(68, r.width() / 4));
  return QRect(r.x() + 10, r.y() + 2, w, h);
}

int slot_offset(int idx) {
  static const int pattern[] = {0, -52, 52, -104, 104, -156, 156};
  const int n = (int)(sizeof(pattern) / sizeof(pattern[0]));
  return pattern[idx % n];
}

} // namespace

QPointF tutorial_overlay_step1_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect) {
  if (id == "search_box" && !anchors.search_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.search_anchor_rect);
  }
  if (id == "nfz_btn" && !anchors.nfz_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.nfz_anchor_rect);
  }
  if (id == "dark_mode_btn" && !anchors.dark_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.dark_anchor_rect);
  }
  if (id == "guide_btn" && !anchors.guide_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.guide_anchor_rect);
  }
  if (id == "lang_btn" && !anchors.lang_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.lang_anchor_rect);
  }
  if (id == "osm_llh" && !anchors.osm_llh_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(status_text_head(anchors.osm_llh_anchor_rect));
  }
  return ctx.ray_rect.center();
}

QPointF tutorial_overlay_step2_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect) {
  if (id == "osm_llh" && !anchors.osm_llh_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(status_text_head(anchors.osm_llh_anchor_rect));
  }
  if (id == "nfz_restricted" && !anchors.nfz_restricted_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.nfz_restricted_anchor_rect);
  }
  if (id == "osm_runtime" && !anchors.osm_runtime_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.osm_runtime_anchor_rect);
  }
  if (id == "osm_stop_btn" && !anchors.osm_stop_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.osm_stop_anchor_rect);
  }
  if (id == "straight_path" || id == "smart_path" || id == "mouse_hint") {
    const QPointF center = ctx.ray_rect.center();
    const QPointF left_btn(center.x() - 36.0, center.y() - 64.0);
    const QPointF right_btn(center.x() + 36.0, center.y() - 64.0);
    if (id == "mouse_hint") {
      return right_btn;
    }
    return left_btn;
  }
  return ctx.ray_rect.center();
}

QPointF tutorial_overlay_step3_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect) {
  if (id == "wave_1" && !anchors.wave_1_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.wave_1_anchor_rect);
  }
  if (id == "wave_2" && !anchors.wave_2_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.wave_2_anchor_rect);
  }
  if (id == "wave_3" && !anchors.wave_3_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.wave_3_anchor_rect);
  }
  if (id == "wave_4" && !anchors.wave_4_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.wave_4_anchor_rect);
  }
  return ctx.ray_rect.center();
}

QPointF tutorial_overlay_step4_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect) {
  if (id == "sig_gear" && !anchors.sig_gear_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_gear_anchor_rect) + QPointF(-4.0, -8.0);
  }
  if (id == "sig_utc" && !anchors.sig_utc_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_utc_anchor_rect) + QPointF(6.0, -4.0);
  }
  if (id == "sig_bdt_gpst") {
    if (!anchors.sig_bdt_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(anchors.sig_bdt_anchor_rect) + QPointF(0.0, 2.0);
    }
    if (!anchors.sig_gpst_anchor_rect.isEmpty()) {
      return map_anchor_to_ray_rect(anchors.sig_gpst_anchor_rect) + QPointF(4.0, 6.0);
    }
  }
  if (id == "sig_rnx" && !anchors.sig_gpst_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_gpst_anchor_rect) + QPointF(-6.0, 8.0);
  }
  if (id == "sig_tab_simple" && !anchors.sig_tab_simple_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_tab_simple_anchor_rect) + QPointF(-8.0, -4.0);
  }
  if (id == "sig_tab_detail" && !anchors.sig_tab_detail_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_tab_detail_anchor_rect) + QPointF(8.0, -4.0);
  }
  if (id == "sig_interfere" && !anchors.sig_interfere_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_interfere_anchor_rect) + QPointF(0.0, 2.0);
  }
  if (id == "sig_system" && !anchors.sig_system_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_system_anchor_rect) + QPointF(0.0, 2.0);
  }
  if (id == "sig_fs" && !anchors.sig_fs_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_fs_anchor_rect) + QPointF(0.0, 2.0);
  }
  if (id == "sig_tx" && !anchors.sig_tx_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_tx_anchor_rect) + QPointF(0.0, 2.0);
  }
  if (id == "sig_start" && !anchors.sig_start_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_start_anchor_rect) + QPointF(-10.0, 8.0);
  }
  if (id == "sig_exit" && !anchors.sig_exit_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sig_exit_anchor_rect) + QPointF(-10.0, 12.0);
  }
  return ctx.ray_rect.center();
}

QPointF tutorial_overlay_step7_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect) {
  if (id == "detail_sats" && !anchors.detail_sats_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.detail_sats_anchor_rect);
  }
  if (id == "gain_slider" && !anchors.gain_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.gain_slider_anchor_rect);
  }
  if (id == "cn0_slider" && !anchors.cn0_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.cn0_slider_anchor_rect);
  }
  if (id == "path_v_slider" && !anchors.path_v_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.path_v_slider_anchor_rect);
  }
  if (id == "path_a_slider" && !anchors.path_a_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.path_a_slider_anchor_rect);
  }
  if (id == "prn_slider" && !anchors.prn_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.prn_slider_anchor_rect);
  }
  if (id == "ch_slider" && !anchors.ch_slider_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.ch_slider_anchor_rect);
  }
  if (id == "sw_fmt" && !anchors.sw_fmt_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sw_fmt_anchor_rect);
  }
  if (id == "sw_mode" && !anchors.sw_mode_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.sw_mode_anchor_rect);
  }
  if (id == "tg_meo" && !anchors.tg_meo_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.tg_meo_anchor_rect);
  }
  if (id == "tg_iono" && !anchors.tg_iono_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.tg_iono_anchor_rect);
  }
  if (id == "tg_clk" && !anchors.tg_clk_anchor_rect.isEmpty()) {
    return map_anchor_to_ray_rect(anchors.tg_clk_anchor_rect);
  }
  return ctx.ray_rect.center();
}

QPointF tutorial_overlay_step1_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect,
    int *slot_left, int *slot_right, int *slot_top, int *slot_bottom) {
  const double cx = ctx.ray_rect.center().x();
  const double cy = ctx.ray_rect.center().y();
  const double dx = anchor.x() - cx;
  const double dy = anchor.y() - cy;

  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 170.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 170.0);
  const double top_y = std::max(10.0 + half_h, ctx.ray_rect.top() - 86.0);
  const double bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                   ctx.ray_rect.bottom() + 86.0);
  const double lower_bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                         ctx.ray_rect.bottom() + 132.0);

  if (id == "search_box") {
    const double y = std::max(10.0 + half_h,
                              std::min((double)ctx.win_height - 58.0 - half_h,
                                       anchor.y() + slot_offset((*slot_left)++)));
    return QPointF(left_x, y);
  }
  if (id == "nfz_btn" || id == "lang_btn") {
    const double x = std::max(10.0 + half_w,
                              std::min((double)ctx.win_width - 10.0 - half_w,
                                       anchor.x() + slot_offset((*slot_top)++)));
    return QPointF(x, top_y);
  }
  if (id == "dark_mode_btn") {
    const QPointF dark_anchor = map_anchor_to_ray_rect(anchors.dark_anchor_rect);
    const double y = std::max(10.0 + half_h,
                              std::min((double)ctx.win_height - 58.0 - half_h,
                                       dark_anchor.y() - 34.0));
    return QPointF(right_x, y);
  }
  if (id == "guide_btn") {
    const QPointF dark_anchor = map_anchor_to_ray_rect(anchors.dark_anchor_rect);
    const QPointF guide_anchor = map_anchor_to_ray_rect(anchors.guide_anchor_rect);
    const double dark_y = std::max(10.0 + half_h,
                                   std::min((double)ctx.win_height - 58.0 - half_h,
                                            dark_anchor.y() - 34.0));
    const double min_gap = std::max(72.0, half_h * 2.0 + 16.0);
    const double preferred = guide_anchor.y() + 34.0;
    const double y =
        std::max(dark_y + min_gap,
                 std::min((double)ctx.win_height - 58.0 - half_h, preferred));
    return QPointF(right_x, y);
  }
  if (id == "osm_llh") {
    return QPointF(left_x, lower_bottom_y);
  }

  const bool horizontal = std::abs(dx) >= std::abs(dy) * 1.12;
  if (horizontal) {
    if (dx >= 0.0) {
      const double y =
          std::max(10.0 + half_h,
                   std::min((double)ctx.win_height - 58.0 - half_h,
                            anchor.y() + slot_offset((*slot_right)++)));
      return QPointF(right_x, y);
    }
    const double y = std::max(10.0 + half_h,
                              std::min((double)ctx.win_height - 58.0 - half_h,
                                       anchor.y() + slot_offset((*slot_left)++)));
    return QPointF(left_x, y);
  }

  if (dy < 0.0) {
    const double x = std::max(10.0 + half_w,
                              std::min((double)ctx.win_width - 10.0 - half_w,
                                       anchor.x() + slot_offset((*slot_top)++)));
    return QPointF(x, top_y);
  }
  const double x = std::max(10.0 + half_w,
                            std::min((double)ctx.win_width - 10.0 - half_w,
                                     anchor.x() + slot_offset((*slot_bottom)++)));
  return QPointF(x, bottom_y);
}

QPointF tutorial_overlay_step2_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx) {
  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double center_y = ctx.ray_rect.center().y();
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 170.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 170.0);
  const double far_left_x =
      std::max(10.0 + half_w, ctx.ray_rect.left() - half_w - 46.0);
  const double far_right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + half_w + 46.0);
  const double bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                   ctx.ray_rect.bottom() + 96.0);
  const double lower_bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                         ctx.ray_rect.bottom() + 132.0);

  if (id == "smart_path") {
    const double x = std::min(left_x, far_left_x);
    const double y =
        std::max(10.0 + half_h, center_y - std::max(94.0, half_h + 26.0));
    return QPointF(x, y);
  }
  if (id == "straight_path") {
    const double x = std::min(left_x, far_left_x);
    const double y = std::min((double)ctx.win_height - 58.0 - half_h,
                              center_y + std::max(20.0, half_h * 0.18));
    return QPointF(x, y);
  }
  if (id == "mouse_hint") {
    const double x = std::max(right_x, far_right_x);
    const double y = center_y;
    return QPointF(x, y);
  }
  if (id == "osm_llh") {
    return QPointF(left_x, lower_bottom_y);
  }
  if (id == "new_user") {
    const double user_y =
        std::max(10.0 + half_h,
                 std::min((double)ctx.win_height - 58.0 - half_h, anchor.y() - 26.0));
    const double user_x = std::max(left_x, std::min(left_x + 36.0, anchor.x() - 18.0));
    return QPointF(user_x, user_y);
  }
  if (id == "nfz_restricted") {
    const double y =
        std::max(10.0 + half_h, ctx.ray_rect.top() + ctx.ray_rect.height() * 0.32);
    return QPointF(right_x, y);
  }
  if (id == "osm_runtime") {
    const double box_x =
        std::max(10.0 + half_w, ctx.ray_rect.center().x() - half_w - 30.0);
    return QPointF(box_x, lower_bottom_y);
  }
  if (id == "osm_stop_btn") {
    const double box_x = std::min((double)ctx.win_width - 10.0 - half_w,
                                  ctx.ray_rect.center().x() + half_w + 30.0);
    return QPointF(box_x, lower_bottom_y);
  }

  return QPointF(right_x, bottom_y);
}

QPointF tutorial_overlay_step4_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx) {
  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 180.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 180.0);
  const double top_y = std::max(10.0 + half_h, ctx.ray_rect.top() - 88.0);
  const double bottom_y =
      std::min((double)ctx.win_height - 58.0 - half_h, ctx.ray_rect.bottom() + 96.0);

  if (id == "wave_1") {
    return QPointF(left_x, std::min(top_y, anchor.y() - 34.0));
  }
  if (id == "wave_2") {
    return QPointF(left_x,
                   std::max(top_y + 60.0, std::min(bottom_y, anchor.y() + 34.0)));
  }
  if (id == "wave_3") {
    return QPointF(right_x, std::min(top_y, anchor.y() - 34.0));
  }
  if (id == "wave_4") {
    return QPointF(right_x,
                   std::max(top_y + 60.0, std::min(bottom_y, anchor.y() + 34.0)));
  }
  return QPointF(right_x, bottom_y);
}

QPointF tutorial_overlay_step5_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx) {
  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 190.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 190.0);
  const double top_y = std::max(10.0 + half_h, ctx.ray_rect.top() - 96.0);
  const double bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                   ctx.ray_rect.bottom() + 100.0);

  const double x_near =
      std::max(10.0 + half_w,
               std::min((double)ctx.win_width - 10.0 - half_w, anchor.x()));
  const double y_near =
      std::max(10.0 + half_h,
               std::min((double)ctx.win_height - 58.0 - half_h, anchor.y()));

  const double top_y_2 =
      std::min((double)ctx.win_height - 58.0 - half_h, top_y + 74.0);
  const double right_inner_x =
      std::max(10.0 + half_w,
               std::min((double)ctx.win_width - 10.0 - half_w, right_x - 14.0));
  const double lower_left_y =
      std::min((double)ctx.win_height - 58.0 - half_h, ctx.ray_rect.bottom() - 26.0);
  const double lower_right_y =
      std::min((double)ctx.win_height - 58.0 - half_h, ctx.ray_rect.bottom() - 26.0);

  if (id == "sig_gear") return QPointF(left_x, top_y);
  if (id == "sig_utc") return QPointF(ctx.ray_rect.center().x(), top_y);
  if (id == "sig_bdt_gpst") {
    const double pinned_x =
        std::max(10.0 + half_w,
                 std::min((double)ctx.win_width - 10.0 - half_w,
                          ctx.ray_rect.left() - 28.0 - half_w));
    const double pinned_y =
        std::max(10.0 + half_h,
                 std::min((double)ctx.win_height - 58.0 - half_h,
                          ctx.ray_rect.top() + 16.0 + half_h + 28.0));
    return QPointF(pinned_x, pinned_y);
  }
  if (id == "sig_rnx") return QPointF(right_inner_x, top_y_2);
  if (id == "sig_tab_simple") return QPointF(left_x, lower_left_y);
  if (id == "sig_tab_detail") return QPointF(right_x, lower_right_y);

  const double d_left = std::abs(anchor.x() - ctx.ray_rect.left());
  const double d_right = std::abs(anchor.x() - ctx.ray_rect.right());
  const double d_top = std::abs(anchor.y() - ctx.ray_rect.top());
  const double d_bottom = std::abs(anchor.y() - ctx.ray_rect.bottom());
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
}

QPointF tutorial_overlay_step6_target_from_anchor(
  const QString &id, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx) {
  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 190.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 190.0);
  const double top_y = std::max(10.0 + half_h, ctx.ray_rect.top() - 96.0);
  const double mid_y =
      std::max(10.0 + half_h,
               std::min((double)ctx.win_height - 58.0 - half_h,
                        ctx.ray_rect.center().y() - 6.0));
  const double low_y = std::max(10.0 + half_h,
                                std::min((double)ctx.win_height - 58.0 - half_h,
                                         ctx.ray_rect.bottom() - 34.0));
  const double bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                   ctx.ray_rect.bottom() + 94.0);
  const double bottom_center_x =
      std::max(10.0 + half_w,
               std::min((double)ctx.win_width - 10.0 - half_w,
                        ctx.ray_rect.center().x()));

  if (id == "sig_interfere") return QPointF(left_x, top_y);
  if (id == "sig_system") return QPointF(right_x, top_y);
  if (id == "sig_fs") return QPointF(left_x, mid_y);
  if (id == "sig_tx") return QPointF(left_x, low_y);
  if (id == "sig_start") return QPointF(bottom_center_x, bottom_y);
  if (id == "sig_exit") return QPointF(right_x, bottom_y);

  return QPointF(right_x, bottom_y);
}

QPointF tutorial_overlay_step7_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors) {
  const double half_w = std::max(50.0, box_size.width() * 0.5);
  const double half_h = std::max(22.0, box_size.height() * 0.5);
  const double left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 190.0);
  const double right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 190.0);
  const double top_y = std::max(10.0 + half_h, ctx.ray_rect.top() - 96.0);
  const double mid_y =
      std::max(10.0 + half_h,
               std::min((double)ctx.win_height - 58.0 - half_h,
                        ctx.ray_rect.center().y() - 2.0));
  const double bottom_y = std::min((double)ctx.win_height - 58.0 - half_h,
                                   ctx.ray_rect.bottom() + 94.0);
  const double bottom_center_x =
      std::max(10.0 + half_w,
               std::min((double)ctx.win_width - 10.0 - half_w,
                        ctx.ray_rect.center().x()));
  const double bottom_left_x = std::max(10.0 + half_w, ctx.ray_rect.left() - 90.0);
  const double bottom_right_x =
      std::min((double)ctx.win_width - 10.0 - half_w, ctx.ray_rect.right() + 90.0);
  const double aspect = (double)ctx.win_width / std::max(1, ctx.win_height);
  const double upper_y = top_y + ((aspect < 1.7) ? 30.0 : 18.0);

  if (id == "detail_sats") return QPointF(bottom_center_x, top_y);
  if (id == "gain_slider") return QPointF(left_x, upper_y);
  if (id == "cn0_slider") return QPointF(right_x, top_y);
  if (id == "path_v_slider") return QPointF(left_x, mid_y);
  if (id == "path_a_slider") return QPointF(right_x, mid_y);
  if (id == "prn_slider") {
    const double prn_x =
        std::max(10.0 + half_w,
                 std::min((double)ctx.win_width - 10.0 - half_w, anchor.x()));
    return QPointF(prn_x, bottom_y);
  }
  if (id == "ch_slider") {
    const double ch_x =
        std::max(10.0 + half_w,
                 std::min((double)ctx.win_width - 10.0 - half_w, anchor.x()));
    return QPointF(ch_x, bottom_y);
  }

  if (ctx.step == 8) {
    const double top_fmt_y =
        std::max(10.0 + half_h,
                 std::min((double)ctx.win_height - 58.0 - half_h, top_y + 4.0));
    const double mid_mode_y =
        std::max(10.0 + half_h,
                 std::min((double)ctx.win_height - 58.0 - half_h, mid_y + 8.0));
    if (id == "sw_fmt") return QPointF(left_x, top_fmt_y);
    if (id == "sw_mode") return QPointF(left_x, mid_mode_y);
    if (id == "tg_meo") return QPointF(bottom_left_x, bottom_y);
    if (id == "tg_iono") return QPointF(bottom_center_x, bottom_y);
    if (id == "tg_clk") return QPointF(bottom_right_x, bottom_y);
  }

  if (!anchors.step6_two_column_layout) {
    if (id == "sw_fmt") return QPointF(left_x, mid_y);
    if (id == "sw_mode") return QPointF(right_x, mid_y);
    if (id == "tg_meo") return QPointF(bottom_left_x, bottom_y);
    if (id == "tg_iono") return QPointF(bottom_center_x, bottom_y);
    if (id == "tg_clk") return QPointF(bottom_right_x, bottom_y);
  } else {
    const double left_lower_y =
        std::min((double)ctx.win_height - 58.0 - half_h, mid_y + 56.0);
    const double right_bridge_x =
        std::max(bottom_center_x + 24.0,
                 std::min(bottom_right_x - 24.0,
                          (bottom_center_x + bottom_right_x) * 0.5));
    if (id == "sw_fmt") return QPointF(left_x, mid_y);
    if (id == "sw_mode") return QPointF(left_x, left_lower_y);
    if (id == "tg_meo") return QPointF(bottom_center_x, bottom_y);
    if (id == "tg_iono") return QPointF(right_bridge_x, bottom_y);
    if (id == "tg_clk") return QPointF(right_x, mid_y + 18.0);
  }

  return QPointF(right_x, bottom_y);
}
