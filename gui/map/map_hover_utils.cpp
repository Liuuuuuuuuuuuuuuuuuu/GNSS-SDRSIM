#include "gui/map/map_hover_utils.h"

int map_hover_region_for_pos(const QPoint &pos, const MapHoverHelpInput &in,
                             QString *text, QRect *anchor) {
  (void)pos;
  (void)in;
  if (text) text->clear();
  if (anchor) *anchor = QRect();
  return -1;
}

void map_draw_hover_help_overlay(QPainter &p, const MapHoverHelpInput &in,
                                 bool hover_visible, const QString &hover_text,
                                 const QRect &hover_anchor) {
  (void)p;
  (void)in;
  (void)hover_visible;
  (void)hover_text;
  (void)hover_anchor;
}
