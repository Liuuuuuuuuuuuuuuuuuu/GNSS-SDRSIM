#ifndef TUTORIAL_OVERLAY_CALLOUT_RULES_UTILS_H
#define TUTORIAL_OVERLAY_CALLOUT_RULES_UTILS_H

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QSize>

#include <functional>

struct TutorialOverlayAnchorRects {
  QRect search_anchor_rect;
  QRect nfz_anchor_rect;
  QRect dark_anchor_rect;
  QRect guide_anchor_rect;
  QRect lang_anchor_rect;
  QRect osm_llh_anchor_rect;
  QRect nfz_restricted_anchor_rect;
  QRect osm_runtime_anchor_rect;
  QRect osm_stop_anchor_rect;

  QRect wave_1_anchor_rect;
  QRect wave_2_anchor_rect;
  QRect wave_3_anchor_rect;
  QRect wave_4_anchor_rect;

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
  QRect gain_slider_anchor_rect;
  QRect cn0_slider_anchor_rect;
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
};

struct TutorialOverlayFrameContext {
  QRectF ray_rect;
  int win_width = 0;
  int win_height = 0;
  int step = 0;
};

QPointF tutorial_overlay_step1_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect);

QPointF tutorial_overlay_step2_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect);

QPointF tutorial_overlay_step3_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect);

QPointF tutorial_overlay_step4_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect);

QPointF tutorial_overlay_step7_anchor_from_id(
    const QString &id, const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect);

QPointF tutorial_overlay_step1_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors,
    const std::function<QPointF(const QRect &)> &map_anchor_to_ray_rect,
    int *slot_left, int *slot_right, int *slot_top, int *slot_bottom);

QPointF tutorial_overlay_step2_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx);

QPointF tutorial_overlay_step4_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx);

QPointF tutorial_overlay_step5_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx);

QPointF tutorial_overlay_step6_target_from_anchor(
    const QString &id, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx);

QPointF tutorial_overlay_step7_target_from_anchor(
    const QString &id, const QPointF &anchor, const QSize &box_size,
    const TutorialOverlayFrameContext &ctx,
    const TutorialOverlayAnchorRects &anchors);

#endif