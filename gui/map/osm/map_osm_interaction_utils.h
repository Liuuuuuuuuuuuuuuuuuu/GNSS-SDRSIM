#ifndef MAP_OSM_INTERACTION_UTILS_H
#define MAP_OSM_INTERACTION_UTILS_H

#include <QPoint>
#include <QRect>

#include <functional>
#include <vector>

struct MapOsmPressRects {
    QRect osm_panel_rect;
    QRect osm_scale_bar_rect;
    QRect osm_stop_btn_rect;
    QRect osm_launch_btn_rect;
    QRect dark_mode_btn_rect;
    QRect search_return_btn_rect;
    QRect nfz_btn_rect;
    QRect back_btn_rect;
    std::vector<QRect> nfz_legend_row_rects;
    bool show_search_return = false;
};

struct MapOsmPressState {
    bool *dragging_osm = nullptr;
    bool *drag_moved_osm = nullptr;
    QPoint *drag_last_pos = nullptr;
};

struct MapOsmPressActions {
    std::function<void()> toggle_scale_ruler;
    std::function<void()> stop_simulation;
    std::function<void()> launch_signal;
    std::function<void()> toggle_dark_mode;
    std::function<void()> restore_search;
    std::function<void()> toggle_nfz;
    std::function<void(int)> toggle_nfz_layer;
    std::function<void()> try_undo_last_segment;
    std::function<void()> confirm_preview_segment;
    std::function<void()> update_all;
    std::function<void(const QRect &)> update_rect;
};

bool map_osm_handle_press(const QPoint &pos, Qt::MouseButton button,
                                                    bool running_ui, bool jam_locked,
                                                    const MapOsmPressRects &rects,
                                                    MapOsmPressState *state,
                                                    const MapOsmPressActions &actions);

bool map_osm_handle_double_click(const QPoint &pos, bool left_button,
                                 const QRect &osm_panel_rect,
                                 bool jam_locked, bool running_ui,
                                 bool *suppress_left_click_release,
                                 const std::function<void(const QPoint &)> &on_set_preview_target_plan,
                                 const std::function<void(const QRect &)> &on_update_rect);

bool map_osm_handle_wheel(const QPoint &pos, int delta,
                          const QRect &osm_panel_rect, bool jam_locked,
                          int zoom_max,
                          int *osm_zoom, double *osm_center_px_x,
                          double *osm_center_px_y,
                          const std::function<void()> &on_normalize_osm_center,
                          const std::function<void()> &on_request_visible_tiles,
                          const std::function<void()> &on_notify_nfz_viewport_changed,
                          const std::function<void(const QRect &)> &on_update_rect);

bool map_osm_handle_move(const QPoint &pos, Qt::MouseButtons buttons,
                         const QRect &osm_panel_rect, bool jam_locked,
                         bool *dragging_osm, bool *drag_moved_osm,
                         QPoint *drag_last_pos, double *osm_center_px_x,
                         double *osm_center_px_y,
                         const std::function<void()> &on_clear_hover_help,
                         const std::function<void()> &on_normalize_osm_center,
                         const std::function<void()> &on_request_visible_tiles,
                         const std::function<void()> &on_notify_nfz_viewport_changed,
                         const std::function<void(const QRect &)> &on_update_rect);

bool map_osm_handle_left_release(
    const QPoint &pos, const QRect &osm_panel_rect, bool jam_locked,
    bool running_ui, bool *suppress_left_click_release, bool *dragging_osm,
    bool *drag_moved_osm,
    const std::function<void(const QPoint &)> &on_set_preview_target_line,
    const std::function<bool(const QPoint &)> &on_try_pick_nfz_target,
    const std::function<void(const QPoint &)> &on_set_selected_llh_from_point,
    const std::function<void(const QRect &)> &on_update_rect);

#endif