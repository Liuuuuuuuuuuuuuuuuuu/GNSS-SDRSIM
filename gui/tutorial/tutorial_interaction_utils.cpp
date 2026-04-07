#include "gui/tutorial/tutorial_interaction_utils.h"

#include "gui/tutorial/tutorial_flow_utils.h"

#include <algorithm>

bool tutorial_sync_control_panel_page(bool overlay_visible, int tutorial_step,
                                      bool *show_detailed_ctrl) {
  if (!overlay_visible || !show_detailed_ctrl) {
    return false;
  }

  bool desired_detail = (tutorial_step >= 6);
  if (*show_detailed_ctrl != desired_detail) {
    *show_detailed_ctrl = desired_detail;
    return true;
  }
  return false;
}

bool tutorial_handle_click(const QPoint &pos, bool running_ui, int last_step,
                           const QRect &toggle_rect, const QRect &prev_rect,
                           const QRect &next_rect, const QRect &close_rect,
                           bool *tutorial_enabled,
                           bool *tutorial_overlay_visible, int *tutorial_step,
                           int *tutorial_anim_step_anchor,
                           int *tutorial_spotlight_index, int *tutorial_text_page,
                           int *tutorial_text_page_count,
                           int *tutorial_text_page_anchor_step,
                           int *tutorial_text_page_anchor_spotlight) {
  if (!tutorial_enabled || !tutorial_overlay_visible || !tutorial_step ||
      !tutorial_anim_step_anchor || !tutorial_spotlight_index ||
      !tutorial_text_page || !tutorial_text_page_count ||
      !tutorial_text_page_anchor_step || !tutorial_text_page_anchor_spotlight) {
    return false;
  }

  if (running_ui) {
    *tutorial_overlay_visible = false;
    *tutorial_enabled = false;
    return false;
  }

  if (toggle_rect.contains(pos)) {
    *tutorial_enabled = !*tutorial_enabled;
    *tutorial_overlay_visible = *tutorial_enabled;
    *tutorial_anim_step_anchor = -1;
    *tutorial_spotlight_index = 0;
    *tutorial_text_page = 0;
    *tutorial_text_page_count = 1;
    *tutorial_text_page_anchor_step = -1;
    *tutorial_text_page_anchor_spotlight = -1;
    if (*tutorial_overlay_visible) {
      *tutorial_step = 0;
    }
    return true;
  }

  if (!*tutorial_overlay_visible) {
    return false;
  }

  if (close_rect.contains(pos)) {
    *tutorial_overlay_visible = false;
    *tutorial_enabled = false;
    return true;
  }

  if (prev_rect.contains(pos)) {
    if (*tutorial_text_page_count > 1 && *tutorial_text_page > 0) {
      *tutorial_text_page -= 1;
      return true;
    }

    int count_here = tutorial_spotlight_count_for_step(*tutorial_step);
    if (count_here > 0 && *tutorial_spotlight_index > 0) {
      *tutorial_spotlight_index -= 1;
    } else {
      *tutorial_step = std::max(0, *tutorial_step - 1);
      int count_prev = tutorial_spotlight_count_for_step(*tutorial_step);
      *tutorial_spotlight_index = (count_prev > 0) ? (count_prev - 1) : 0;
    }
    *tutorial_anim_step_anchor = -1;
    *tutorial_text_page = 0;
    *tutorial_text_page_anchor_step = -1;
    return true;
  }

  if (next_rect.contains(pos)) {
    if (*tutorial_text_page_count > 1 && *tutorial_text_page < (*tutorial_text_page_count - 1)) {
      *tutorial_text_page += 1;
      return true;
    }

    int count_here = tutorial_spotlight_count_for_step(*tutorial_step);
    if (count_here > 0 && *tutorial_spotlight_index < (count_here - 1)) {
      *tutorial_spotlight_index += 1;
    } else {
      *tutorial_spotlight_index = 0;
      if (*tutorial_step < last_step) {
        *tutorial_step += 1;
        *tutorial_anim_step_anchor = -1;
      } else {
        *tutorial_overlay_visible = false;
        *tutorial_enabled = false;
      }
    }
    *tutorial_text_page = 0;
    *tutorial_text_page_anchor_step = -1;
    return true;
  }

  return true;
}