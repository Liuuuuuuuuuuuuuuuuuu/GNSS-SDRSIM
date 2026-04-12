#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

#include "gui/layout/geometry/control_layout.h"

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in) {
  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.detailed);

  const QRect left_map = in.osm_panel_rect.adjusted(6, 6, -6, -6);
  if (in.step == 0) {
    return QRect();
  }
  if (in.step == 1) {
    return QRect(left_map.x(), left_map.y(), left_map.width(),
                 std::max(12, left_map.height() / 2));
  }
  if (in.step == 2) {
    return QRect(left_map.x(), left_map.y() + std::max(12, left_map.height() / 2),
                 left_map.width(), std::max(12, left_map.height() / 2));
  }
  if (in.step == 3) {
    int map_x = in.win_width / 2;
    int map_y = 0;
    int map_w = in.win_width - map_x;
    int map_h = in.win_height / 2;
    return QRect(map_x + 6, map_y + 6, std::max(10, map_w - 12),
                 std::max(10, map_h - 12));
  }
  if (in.step == 4) {
    int x = in.win_width / 2;
    int y = in.win_height / 2;
    int w = in.win_width - x;
    int h = in.win_height - y;
    return QRect(x + 6, y + 6, std::max(10, w - 12), std::max(10, h - 12));
  }
  if (in.step == 5) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  if (in.step == 6) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  if (in.step == 7 || in.step == 8) {
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  return QRect();
}
