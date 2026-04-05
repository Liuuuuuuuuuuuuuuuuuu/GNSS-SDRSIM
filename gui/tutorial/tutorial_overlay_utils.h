#ifndef TUTORIAL_OVERLAY_UTILS_H
#define TUTORIAL_OVERLAY_UTILS_H

#include <QPainter>
#include <QRect>

#include <chrono>

struct TutorialOverlayInput {
  int win_width = 0;
  int win_height = 0;
  bool overlay_visible = false;
  bool running_ui = false;
  bool detailed = false;
  int step = 0;
  QRect osm_panel_rect;
  QRect osm_stop_btn_rect;
  int last_step = 0;
};

struct TutorialOverlayState {
  QRect prev_btn_rect;
  QRect next_btn_rect;
  QRect close_btn_rect;
  int anim_step_anchor = -1;
  std::chrono::steady_clock::time_point anim_start_tp;
  int spotlight_index = 0;
  int text_page = 0;
  int text_page_count = 1;
  int text_page_anchor_step = -1;
  int text_page_anchor_spotlight = -1;
};

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in);
void tutorial_draw_overlay(QPainter &p, const TutorialOverlayInput &in,
                           TutorialOverlayState *state);

#endif