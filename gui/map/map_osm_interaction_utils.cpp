#include "gui/map/map_osm_interaction_utils.h"

#include "gui/geo/osm_projection.h"

#include <algorithm>

#include <cmath>

bool map_osm_handle_press(const QPoint &pos, Qt::MouseButton button,
                          bool running_ui, bool jam_locked,
                          const MapOsmPressRects &rects,
                          MapOsmPressState *state,
                          const MapOsmPressActions &actions) {
  if (button == Qt::LeftButton) {
    if (!rects.osm_scale_bar_rect.isEmpty() &&
        rects.osm_scale_bar_rect.contains(pos)) {
      if (actions.toggle_scale_ruler)
        actions.toggle_scale_ruler();
      if (actions.update_rect)
        actions.update_rect(rects.osm_panel_rect);
      return true;
    }

    if (running_ui && rects.osm_stop_btn_rect.contains(pos)) {
      if (actions.stop_simulation) actions.stop_simulation();
      if (actions.update_all) actions.update_all();
      return true;
    }

    if (jam_locked && !rects.dark_mode_btn_rect.isEmpty() &&
        rects.dark_mode_btn_rect.contains(pos)) {
      return true;
    }

    if (!rects.dark_mode_btn_rect.isEmpty() &&
        rects.dark_mode_btn_rect.contains(pos)) {
      if (actions.toggle_dark_mode) actions.toggle_dark_mode();
      if (actions.update_rect) actions.update_rect(rects.osm_panel_rect);
      return true;
    }

    if (jam_locked && rects.show_search_return &&
        !rects.search_return_btn_rect.isEmpty() &&
        rects.search_return_btn_rect.contains(pos)) {
      return true;
    }

    if (rects.show_search_return &&
        !rects.search_return_btn_rect.isEmpty() &&
        rects.search_return_btn_rect.contains(pos)) {
      if (actions.restore_search) actions.restore_search();
      if (actions.update_rect) actions.update_rect(rects.osm_panel_rect);
      return true;
    }

    if (jam_locked && !rects.nfz_btn_rect.isEmpty() &&
        rects.nfz_btn_rect.contains(pos)) {
      return true;
    }

    if (!rects.nfz_btn_rect.isEmpty() && rects.nfz_btn_rect.contains(pos)) {
      if (actions.toggle_nfz) actions.toggle_nfz();
      if (actions.update_rect) actions.update_rect(rects.osm_panel_rect);
      return true;
    }

    if (rects.osm_panel_rect.contains(pos)) {
      if (jam_locked) {
        if (running_ui && !rects.back_btn_rect.isEmpty() &&
            rects.back_btn_rect.contains(pos)) {
          return true;
        }
        return true;
      }
      if (running_ui && !rects.back_btn_rect.isEmpty() &&
          rects.back_btn_rect.contains(pos)) {
        if (actions.try_undo_last_segment) actions.try_undo_last_segment();
        if (actions.update_rect) actions.update_rect(rects.osm_panel_rect);
        return true;
      }
      if (state && state->dragging_osm && state->drag_moved_osm && state->drag_last_pos) {
        *state->dragging_osm = true;
        *state->drag_moved_osm = false;
        *state->drag_last_pos = pos;
        return true;
      }
    }
  } else if (button == Qt::RightButton) {
    if (running_ui && !jam_locked && rects.osm_panel_rect.contains(pos)) {
      if (actions.confirm_preview_segment) actions.confirm_preview_segment();
      if (actions.update_rect) actions.update_rect(rects.osm_panel_rect);
      return true;
    }
  }
  return false;
}

bool map_osm_handle_move(const QPoint &pos, Qt::MouseButtons buttons,
                         const QRect &osm_panel_rect, bool jam_locked,
                         bool *dragging_osm, bool *drag_moved_osm,
                         QPoint *drag_last_pos, double *osm_center_px_x,
                         double *osm_center_px_y,
                         const std::function<void()> &on_clear_hover_help,
                         const std::function<void()> &on_normalize_osm_center,
                         const std::function<void()> &on_request_visible_tiles,
                         const std::function<void()> &on_notify_nfz_viewport_changed,
                         const std::function<void(const QRect &)> &on_update_rect) {
  if (!dragging_osm || !drag_moved_osm || !drag_last_pos || !osm_center_px_x ||
      !osm_center_px_y) {
    return false;
  }
  if (!(*dragging_osm) || !(buttons & Qt::LeftButton) ||
      !osm_panel_rect.contains(pos)) {
    return false;
  }
  if (jam_locked) {
    *dragging_osm = false;
    return true;
  }

  if (on_clear_hover_help) on_clear_hover_help();

  QPoint delta = pos - *drag_last_pos;
  if (std::abs(delta.x()) + std::abs(delta.y()) >= 2) {
    *drag_moved_osm = true;
  }
  *drag_last_pos = pos;
  *osm_center_px_x -= delta.x();
  *osm_center_px_y -= delta.y();

  if (on_normalize_osm_center) on_normalize_osm_center();
  if (on_request_visible_tiles) on_request_visible_tiles();
  if (on_notify_nfz_viewport_changed) on_notify_nfz_viewport_changed();
  if (on_update_rect) on_update_rect(osm_panel_rect);
  return true;
}

bool map_osm_handle_left_release(
    const QPoint &pos, const QRect &osm_panel_rect, bool jam_locked,
    bool running_ui, bool *suppress_left_click_release, bool *dragging_osm,
    bool *drag_moved_osm,
    const std::function<void(const QPoint &)> &on_set_preview_target_line,
    const std::function<bool(const QPoint &)> &on_try_pick_nfz_target,
    const std::function<void(const QPoint &)> &on_set_selected_llh_from_point,
    const std::function<void(const QRect &)> &on_update_rect) {
  if (!suppress_left_click_release || !dragging_osm || !drag_moved_osm) {
    return false;
  }
  if (*suppress_left_click_release) {
    *suppress_left_click_release = false;
    *dragging_osm = false;
    return true;
  }

  if (!(*dragging_osm) || !osm_panel_rect.contains(pos) || *drag_moved_osm) {
    *dragging_osm = false;
    return false;
  }

  if (jam_locked) {
    *dragging_osm = false;
    return true;
  }

  if (running_ui) {
    if (on_set_preview_target_line) on_set_preview_target_line(pos);
    if (on_update_rect) on_update_rect(osm_panel_rect);
    *dragging_osm = false;
    return true;
  }

  bool hit_nfz = false;
  if (on_try_pick_nfz_target) {
    hit_nfz = on_try_pick_nfz_target(pos);
  }
  if (!hit_nfz && on_set_selected_llh_from_point) {
    on_set_selected_llh_from_point(pos);
  }
  if (on_update_rect) on_update_rect(osm_panel_rect);
  *dragging_osm = false;
  return true;
}

bool map_osm_handle_double_click(const QPoint &pos, bool left_button,
                                 const QRect &osm_panel_rect,
                                 bool jam_locked, bool running_ui,
                                 bool *suppress_left_click_release,
                                 const std::function<void(const QPoint &)> &on_set_preview_target_plan,
                                 const std::function<void(const QRect &)> &on_update_rect) {
  if (!left_button || !osm_panel_rect.contains(pos)) {
    return false;
  }
  if (jam_locked) {
    return true;
  }
  if (running_ui) {
    if (on_set_preview_target_plan) {
      on_set_preview_target_plan(pos);
    }
    if (suppress_left_click_release) {
      *suppress_left_click_release = true;
    }
    if (on_update_rect) {
      on_update_rect(osm_panel_rect);
    }
    return true;
  }
  return false;
}

bool map_osm_handle_wheel(const QPoint &pos, int delta,
                          const QRect &osm_panel_rect, bool jam_locked,
                          int *osm_zoom, double *osm_center_px_x,
                          double *osm_center_px_y,
                          const std::function<void()> &on_normalize_osm_center,
                          const std::function<void()> &on_request_visible_tiles,
                          const std::function<void()> &on_notify_nfz_viewport_changed,
                          const std::function<void(const QRect &)> &on_update_rect) {
  if (!osm_panel_rect.contains(pos)) {
    return false;
  }
  if (jam_locked || !osm_zoom || !osm_center_px_x || !osm_center_px_y) {
    return true;
  }
  if (delta == 0) {
    return true;
  }

  int old_zoom = *osm_zoom;
  int zoom_steps = delta / 120;
  if (zoom_steps == 0) {
    zoom_steps = (delta > 0) ? 1 : -1;
  }
  int new_zoom = std::max(2, std::min(18, old_zoom + zoom_steps));
  if (new_zoom == old_zoom) {
    return true;
  }

  double old_world = osm_world_size_for_zoom(old_zoom);
  double new_world = osm_world_size_for_zoom(new_zoom);
  double scale = new_world / old_world;

  double vp_x = (double)(pos.x() - osm_panel_rect.x());
  double vp_y = (double)(pos.y() - osm_panel_rect.y());
  double old_left = *osm_center_px_x - (double)osm_panel_rect.width() * 0.5;
  double old_top = *osm_center_px_y - (double)osm_panel_rect.height() * 0.5;
  double world_x = old_left + vp_x;
  double world_y = old_top + vp_y;

  *osm_zoom = new_zoom;
  *osm_center_px_x *= scale;
  *osm_center_px_y *= scale;

  double new_world_x = world_x * scale;
  double new_world_y = world_y * scale;
  double new_left = new_world_x - vp_x;
  double new_top = new_world_y - vp_y;
  *osm_center_px_x = new_left + (double)osm_panel_rect.width() * 0.5;
  *osm_center_px_y = new_top + (double)osm_panel_rect.height() * 0.5;

  if (on_normalize_osm_center) {
    on_normalize_osm_center();
  }
  if (on_request_visible_tiles) {
    on_request_visible_tiles();
  }
  if (on_notify_nfz_viewport_changed) {
    on_notify_nfz_viewport_changed();
  }
  if (on_update_rect) {
    on_update_rect(osm_panel_rect);
  }
  return true;
}