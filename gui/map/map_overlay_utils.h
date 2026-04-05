#ifndef MAP_OVERLAY_UTILS_H
#define MAP_OVERLAY_UTILS_H

#include <QPoint>
#include <QRect>

struct MapSearchOverlayLayout {
  QRect search_box_rect;
  QRect results_list_rect;
};

MapSearchOverlayLayout map_overlay_search_layout(int win_width, int win_height);
bool map_overlay_should_show_search(bool tutorial_overlay_visible);
bool map_overlay_search_enabled(bool should_show_search, bool jam_locked);
bool map_overlay_click_outside_search(const QRect &results_rect,
                                      const QRect &search_box_rect,
                                      const QPoint &click_pos);

#endif