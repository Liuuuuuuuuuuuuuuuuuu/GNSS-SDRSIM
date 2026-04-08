#ifndef TUTORIAL_INTERACTION_UTILS_H
#define TUTORIAL_INTERACTION_UTILS_H

#include <QPoint>
#include <QRect>

bool tutorial_sync_control_panel_page(bool overlay_visible, int tutorial_step,
                                      bool *show_detailed_ctrl);

bool tutorial_handle_click(const QPoint &pos, bool running_ui, int last_step,
                           const QRect &toggle_rect, const QRect &prev_rect,
                           const QRect &next_rect, const QRect &close_rect,
                           bool *tutorial_enabled,
                           bool *tutorial_overlay_visible, int *tutorial_step,
                           int *tutorial_anim_step_anchor,
                           int *tutorial_spotlight_index, int *tutorial_text_page,
                           int *tutorial_text_page_count,
                           int *tutorial_text_page_anchor_step,
                           int *tutorial_text_page_anchor_spotlight);

#endif