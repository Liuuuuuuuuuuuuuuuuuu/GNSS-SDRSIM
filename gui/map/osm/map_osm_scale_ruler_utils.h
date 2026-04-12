#ifndef MAP_OSM_SCALE_RULER_UTILS_H
#define MAP_OSM_SCALE_RULER_UTILS_H

#include "gui/map/osm/map_osm_panel_utils.h"

QRect map_osm_draw_scale_bar(QPainter &p, const MapOsmPanelInput &in);

void map_osm_draw_scale_ruler_overlay(QPainter &p,
                                      const MapOsmPanelInput &in);

#endif
