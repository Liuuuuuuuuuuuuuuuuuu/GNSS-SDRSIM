#include "gui/map/osm/map_osm_metrics_utils.h"

#include "gui/geo/osm_projection.h"

#include <cmath>
#include <limits>

namespace map_osm_metrics {

bool llh_is_valid(double lat_deg, double lon_deg) {
  return std::isfinite(lat_deg) && std::isfinite(lon_deg) && lat_deg >= -90.0 &&
         lat_deg <= 90.0 && lon_deg >= -180.0 && lon_deg <= 180.0;
}

double distance_m_for_display(double lat0_deg, double lon0_deg,
                              double lat1_deg, double lon1_deg) {
  const double lat0 = lat0_deg * M_PI / 180.0;
  const double lat1 = lat1_deg * M_PI / 180.0;
  const double dlat = lat1 - lat0;
  const double dlon = wrap_lon_delta_deg(lon1_deg - lon0_deg) * M_PI / 180.0;
  const double mean_lat = 0.5 * (lat0 + lat1);
  const double x = dlon * std::cos(mean_lat) * 6371000.0;
  const double y = dlat * 6371000.0;
  return std::sqrt(x * x + y * y);
}

double polyline_total_distance_m(const std::vector<LonLat> &polyline) {
  if (polyline.size() < 2) {
    return 0.0;
  }
  double total_m = 0.0;
  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    if (!llh_is_valid(a.lat, a.lon) || !llh_is_valid(b.lat, b.lon)) {
      continue;
    }
    total_m += distance_m_for_display(a.lat, a.lon, b.lat, b.lon);
  }
  return total_m;
}

double polyline_remaining_from_pos_m(const std::vector<LonLat> &polyline,
                                     double pos_lat_deg,
                                     double pos_lon_deg) {
  if (polyline.size() < 2 || !llh_is_valid(pos_lat_deg, pos_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  const double kEarthR = 6371000.0;
  std::vector<double> cum_len_m(polyline.size(), 0.0);
  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    double seg_m = 0.0;
    if (llh_is_valid(a.lat, a.lon) && llh_is_valid(b.lat, b.lon)) {
      seg_m = distance_m_for_display(a.lat, a.lon, b.lat, b.lon);
      if (!std::isfinite(seg_m) || seg_m < 0.0) {
        seg_m = 0.0;
      }
    }
    cum_len_m[i] = cum_len_m[i - 1] + seg_m;
  }
  const double total_m = cum_len_m.back();
  if (!(total_m > 0.0)) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  double best_remain_m = std::numeric_limits<double>::infinity();

  for (size_t i = 1; i < polyline.size(); ++i) {
    const auto &a = polyline[i - 1];
    const auto &b = polyline[i];
    if (!llh_is_valid(a.lat, a.lon) || !llh_is_valid(b.lat, b.lon)) {
      continue;
    }

    const double mean_lat_rad = 0.5 * (a.lat + b.lat) * M_PI / 180.0;
    const double bx = wrap_lon_delta_deg(b.lon - a.lon) * M_PI / 180.0 *
                      std::cos(mean_lat_rad) * kEarthR;
    const double by = (b.lat - a.lat) * M_PI / 180.0 * kEarthR;
    const double seg_len_m = std::sqrt(bx * bx + by * by);
    if (!(seg_len_m > 1e-6)) {
      continue;
    }

    const double px = wrap_lon_delta_deg(pos_lon_deg - a.lon) * M_PI / 180.0 *
                      std::cos(mean_lat_rad) * kEarthR;
    const double py = (pos_lat_deg - a.lat) * M_PI / 180.0 * kEarthR;

    const double dot = px * bx + py * by;
    double t = dot / (seg_len_m * seg_len_m);
    if (t < 0.0) {
      t = 0.0;
    }
    if (t > 1.0) {
      t = 1.0;
    }

    const double qx = t * bx;
    const double qy = t * by;
    const double dx = px - qx;
    const double dy = py - qy;
    const double cross_track_m = std::sqrt(dx * dx + dy * dy);

    const double along_m = cum_len_m[i - 1] + t * seg_len_m;
    double remain_m = total_m - along_m;
    if (remain_m < 0.0) {
      remain_m = 0.0;
    }

    const double penalty = cross_track_m * 0.02;
    const double scored_remain = remain_m + penalty;
    if (scored_remain < best_remain_m) {
      best_remain_m = remain_m;
    }
  }

  if (!std::isfinite(best_remain_m)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return best_remain_m;
}

double segment_full_distance_m(const MapOsmPanelSegment &seg) {
  if (seg.mode == MapOsmPanelSegment::PathMode::Line) {
    if (!llh_is_valid(seg.start_lat_deg, seg.start_lon_deg) ||
        !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return distance_m_for_display(seg.start_lat_deg, seg.start_lon_deg,
                                  seg.end_lat_deg, seg.end_lon_deg);
  }

  if (seg.polyline.size() >= 2) {
    const double m = polyline_total_distance_m(seg.polyline);
    if (std::isfinite(m) && m >= 0.0) {
      return m;
    }
  }

  if (!llh_is_valid(seg.start_lat_deg, seg.start_lon_deg) ||
      !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return distance_m_for_display(seg.start_lat_deg, seg.start_lon_deg,
                                seg.end_lat_deg, seg.end_lon_deg);
}

double segment_remaining_from_receiver_m(const MapOsmPanelSegment &seg,
                                         double rx_lat_deg,
                                         double rx_lon_deg) {
  if (!llh_is_valid(rx_lat_deg, rx_lon_deg) ||
      !llh_is_valid(seg.end_lat_deg, seg.end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  if (seg.mode == MapOsmPanelSegment::PathMode::Line) {
    return distance_m_for_display(rx_lat_deg, rx_lon_deg, seg.end_lat_deg,
                                  seg.end_lon_deg);
  }

  const double direct_m =
      distance_m_for_display(rx_lat_deg, rx_lon_deg, seg.end_lat_deg,
                             seg.end_lon_deg);

  if (seg.polyline.size() >= 2) {
    const double remain =
        polyline_remaining_from_pos_m(seg.polyline, rx_lat_deg, rx_lon_deg);
    if (std::isfinite(remain) && remain >= 0.0) {
      return std::max(remain, direct_m);
    }
  }

  return direct_m;
}

double preview_full_distance_m(const MapOsmPanelInput &in) {
  if (!llh_is_valid(in.preview_start_lat_deg, in.preview_start_lon_deg) ||
      !llh_is_valid(in.preview_end_lat_deg, in.preview_end_lon_deg)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (in.preview_mode == MapOsmPanelSegment::PathMode::Line) {
    return distance_m_for_display(in.preview_start_lat_deg,
                                  in.preview_start_lon_deg,
                                  in.preview_end_lat_deg,
                                  in.preview_end_lon_deg);
  }
  if (in.preview_polyline && in.preview_polyline->size() >= 2) {
    const double m = polyline_total_distance_m(*in.preview_polyline);
    if (std::isfinite(m) && m >= 0.0) {
      return m;
    }
  }
  return distance_m_for_display(in.preview_start_lat_deg,
                                in.preview_start_lon_deg,
                                in.preview_end_lat_deg,
                                in.preview_end_lon_deg);
}

} // namespace map_osm_metrics
