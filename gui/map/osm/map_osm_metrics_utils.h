#ifndef MAP_OSM_METRICS_UTILS_H
#define MAP_OSM_METRICS_UTILS_H

#include <vector>

#include "gui/map/osm/map_osm_panel_utils.h"

namespace map_osm_metrics {

bool llh_is_valid(double lat_deg, double lon_deg);

double distance_m_for_display(double lat0_deg, double lon0_deg,
                              double lat1_deg, double lon1_deg);

double polyline_total_distance_m(const std::vector<LonLat> &polyline);

double polyline_remaining_from_pos_m(const std::vector<LonLat> &polyline,
                                     double pos_lat_deg,
                                     double pos_lon_deg);

double segment_full_distance_m(const MapOsmPanelSegment &seg);

double segment_remaining_from_receiver_m(const MapOsmPanelSegment &seg,
                                         double rx_lat_deg,
                                         double rx_lon_deg);

double preview_full_distance_m(const MapOsmPanelInput &in);

} // namespace map_osm_metrics

#endif
