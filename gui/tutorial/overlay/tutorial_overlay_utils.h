#ifndef TUTORIAL_OVERLAY_UTILS_H
#define TUTORIAL_OVERLAY_UTILS_H

#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/state/signal_snapshot.h"

class QWidget;

struct TutorialOverlayInput {
  QWidget *host_widget = nullptr;
  int win_width = 0;
  int win_height = 0;
  GuiLanguage language = GuiLanguage::English;
  bool overlay_visible = false;
  bool running_ui = false;
  bool detailed = false;
  bool has_navigation_data = false;
  int sat_mode = 2; // 0=single-candidate, 1=by-system PRN ranges, 2=show all
  int active_prn_mask_len = 0;
  std::vector<int> active_prn_mask;
  int single_candidate_count = 0;
  std::vector<int> single_candidates;
  uint8_t signal_mode = 0; // 0=BDS, 1=GPS, 2=BDS+GPS
  std::vector<SatPoint> sat_points;
  int step = 0;
  QRect osm_panel_rect;
  QRect osm_stop_btn_rect;
  QRect osm_runtime_rect;
  QRect search_box_rect;
  QRect nfz_btn_rect;
  QRect dark_mode_btn_rect;
  QRect tutorial_toggle_rect;
  QRect lang_btn_rect;
  std::vector<QRect> osm_status_badge_rects;
  std::vector<QRect> osm_nfz_legend_row_rects;
  QRect signal_clean_rect;
  QPixmap signal_clean_snapshot;
  QRect waveform_clean_rect;
  QPixmap waveform_clean_snapshot;
  QRect osm_lower_clean_rect;
  QPixmap osm_lower_clean_snapshot;
  int last_step = 0;
};

struct TutorialOverlayState {
  QRect prev_btn_rect;
  QRect next_btn_rect;
  QRect close_btn_rect;
  QRect contents_btn_rect;            // back-to-contents button (steps 1-7 only)
  int anim_step_anchor = -1;
  std::chrono::steady_clock::time_point anim_start_tp;
  int spotlight_index = 0;
  int text_page = 0;
  int text_page_count = 1;
  int text_page_anchor_step = -1;
  int text_page_anchor_spotlight = -1;
  // TOC jump buttons (valid when step == 0)
  QRect toc_btn_rects[5];
  int toc_btn_targets[5] = {1, 3, 4, 5, 7};
  // Callout hit-test data — rebuilt every frame, used for click detection
  std::vector<QRectF> callout_hit_boxes;
  std::vector<QPointF> callout_hit_anchors;
  // Element glow animation (triggered on callout click)
  bool has_glow = false;
  QPointF glow_anchor;
  int glow_step = -1;
  std::chrono::steady_clock::time_point glow_start_tp;
};

struct TutorialGalaxyCalloutDef {
  QString id;
  QString text;
  double angle_deg = 0.0;
  double radius_px = 220.0;
  QSize box_size = QSize(220, 96);
};

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in);
void tutorial_draw_overlay(QPainter &p, const TutorialOverlayInput &in,
                           TutorialOverlayState *state);

#endif