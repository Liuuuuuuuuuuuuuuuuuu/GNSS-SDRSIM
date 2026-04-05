#include "gui/map/map_overlay_utils.h"

#include <algorithm>

MapSearchOverlayLayout map_overlay_search_layout(int win_width, int win_height) {
  MapSearchOverlayLayout lo;
  lo.search_box_rect = QRect(10, 10, 240, 30);

  const QRect sb = lo.search_box_rect;
  const int max_panel_w = std::max(220, win_width / 2 - 20);
  const int list_w = std::min(460, max_panel_w);
  const int list_h = std::min(240, std::max(120, win_height / 3));
  lo.results_list_rect =
      QRect(sb.x(), sb.y() + sb.height() + 6, list_w, list_h);

  return lo;
}

bool map_overlay_should_show_search(bool tutorial_overlay_visible) {
  return !tutorial_overlay_visible;
}

bool map_overlay_search_enabled(bool should_show_search, bool jam_locked) {
  return should_show_search && !jam_locked;
}

bool map_overlay_click_outside_search(const QRect &results_rect,
                                      const QRect &search_box_rect,
                                      const QPoint &click_pos) {
  return !results_rect.contains(click_pos) &&
         !search_box_rect.contains(click_pos);
}
