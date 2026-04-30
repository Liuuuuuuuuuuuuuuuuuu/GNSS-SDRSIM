#include "gui/nfz/nfz_hit_test_utils.h"

#include "gui/geo/geo_io.h"

#include <cmath>
#include <limits>

namespace {

// 使用射線交點法檢查點是否在多邊形環內
bool point_in_ring(double click_lat, double click_lon, const std::vector<DjiLonLat> &ring) {
  const int ring_size = (int)ring.size();
  if (ring_size < 3) return false;
  
  bool inside = false;
  int j = ring_size - 1;
  for (int i = 0; i < ring_size; ++i) {
    double lat_i = ring[i].lat;
    double lon_i = ring[i].lon;
    double lat_j = ring[j].lat;
    double lon_j = ring[j].lon;

    if ((lat_i > click_lat) != (lat_j > click_lat)) {
      double d_lat = lat_j - lat_i;
      double a = click_lon - lon_i;
      double b = (lon_j - lon_i) * (click_lat - lat_i);
      if (d_lat > 0.0) {
        if (a * d_lat < b)
          inside = !inside;
      } else if (d_lat < 0.0) {
        if (a * d_lat > b)
          inside = !inside;
      }
    }
    j = i;
  }
  return inside;
}

bool polygon_bbox_center(const std::vector<DjiLonLat> &ring, double *lat,
                         double *lon) {
  if (!lat || !lon || ring.empty())
    return false;

  double min_lat = ring[0].lat;
  double max_lat = ring[0].lat;
  double min_lon = ring[0].lon;
  double max_lon = ring[0].lon;
  for (const auto &pt : ring) {
    min_lat = std::min(min_lat, pt.lat);
    max_lat = std::max(max_lat, pt.lat);
    min_lon = std::min(min_lon, pt.lon);
    max_lon = std::max(max_lon, pt.lon);
  }
  *lat = 0.5 * (min_lat + max_lat);
  *lon = 0.5 * (min_lon + max_lon);
  return true;
}

bool nfz_zone_center(const DjiNfzZone &nfz, double *lat, double *lon) {
  if (!lat || !lon)
    return false;

  if (nfz.has_cached_center) {
    *lat = nfz.cached_center_lat;
    *lon = nfz.cached_center_lon;
    return true;
  }

  if (nfz.type == DjiNfzType::CIRCLE) {
    *lat = nfz.center_lat;
    *lon = nfz.center_lon;
    return true;
  }

  if (nfz.type == DjiNfzType::POLYGON && !nfz.rings.empty()) {
    // 使用外環（rings[0]）的幾何重心，避免落點偏向某一側。
    const auto &outer_ring = nfz.rings[0];
    const auto &pts = outer_ring.points;
    if (pts.size() >= 3) {
      double area2 = 0.0;
      double cx_acc = 0.0;
      double cy_acc = 0.0;
      for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const double x0 = pts[j].lon;
        const double y0 = pts[j].lat;
        const double x1 = pts[i].lon;
        const double y1 = pts[i].lat;
        const double cross = x0 * y1 - x1 * y0;
        area2 += cross;
        cx_acc += (x0 + x1) * cross;
        cy_acc += (y0 + y1) * cross;
      }
      if (std::abs(area2) > 1e-12) {
        *lon = cx_acc / (3.0 * area2);
        *lat = cy_acc / (3.0 * area2);
        return true;
      }
    }
    return polygon_bbox_center(pts, lat, lon);
  }

  return false;
}

double wrap_deg_180(double deg) {
  while (deg > 180.0)
    deg -= 360.0;
  while (deg < -180.0)
    deg += 360.0;
  return deg;
}

double bearing_deg_approx(double lat0_deg, double lon0_deg,
                          double lat1_deg, double lon1_deg) {
  const double lat0 = lat0_deg * M_PI / 180.0;
  const double lat1 = lat1_deg * M_PI / 180.0;
  const double dlon = (lon1_deg - lon0_deg) * M_PI / 180.0;
  const double y = std::sin(dlon) * std::cos(lat1);
  const double x = std::cos(lat0) * std::sin(lat1) -
                   std::sin(lat0) * std::cos(lat1) * std::cos(dlon);
  double brg = std::atan2(y, x) * 180.0 / M_PI;
  while (brg < 0.0)
    brg += 360.0;
  while (brg >= 360.0)
    brg -= 360.0;
  return brg;
}

} // namespace

int nfz_zone_layer_index(const DjiNfzZone &zone) {
  if (zone.level == 2)
    return 0; // Restricted/Core (red)
  if (zone.level == 8)
    return 1; // Warning/Alt-limit (yellow)
  if (zone.level == 1)
    return 2; // Authorization (blue)
  return 3;   // Service/other (white outline)
}

bool nfz_pick_target_llh(const std::vector<DjiNfzZone> &zones, double click_lat,
                         double click_lon, double *target_lat,
                         double *target_lon,
                         const bool *layer_visible4) {
  bool found = false;
  int best_layer = std::numeric_limits<int>::max();
  double best_dist = std::numeric_limits<double>::max();
  double best_lat = 0.0;
  double best_lon = 0.0;

  for (const auto &nfz : zones) {
    const int layer_idx = nfz_zone_layer_index(nfz);
    if (layer_visible4 && layer_idx >= 0 && layer_idx < 4 && !layer_visible4[layer_idx]) {
      continue;
    }

    if (nfz.has_outer_bbox &&
        (click_lat < nfz.outer_min_lat || click_lat > nfz.outer_max_lat ||
         click_lon < nfz.outer_min_lon || click_lon > nfz.outer_max_lon)) {
      continue;
    }

    bool hit = false;
    double center_lat = 0.0;
    double center_lon = 0.0;

    if (nfz.type == DjiNfzType::CIRCLE) {
      if (distance_m_approx(click_lat, click_lon, nfz.center_lat,
                            nfz.center_lon) <= nfz.radius_m) {
        hit = true;
        center_lat = nfz.center_lat;
        center_lon = nfz.center_lon;
      }
    }

    if (!hit && nfz.type == DjiNfzType::POLYGON && !nfz.rings.empty()) {
      // 檢查點是否在外環內
      bool inside_outer = point_in_ring(click_lat, click_lon, nfz.rings[0].points);
      
      if (inside_outer) {
        // 檢查點是否在任何內環（洞）內
        bool inside_hole = false;
        for (size_t k = 1; k < nfz.rings.size(); ++k) {
          if (nfz.rings[k].is_outer == false) {
            if (point_in_ring(click_lat, click_lon, nfz.rings[k].points)) {
              inside_hole = true;
              break;
            }
          }
        }
        
        // 如果在外環內且不在任何洞內，則在禁飛區內
        if (!inside_hole) {
          hit = true;
          if (!nfz_zone_center(nfz, &center_lat, &center_lon)) {
            const auto &outer = nfz.rings[0].points;
            if (!polygon_bbox_center(outer, &center_lat, &center_lon)) {
              center_lat = click_lat;
              center_lon = click_lon;
            }
          }
        }
      }
    }

    if (!hit)
      continue;

    // 優先規則：先選最上層（layer index 小），同層再選中心最接近點擊處。
    const double dist = distance_m_approx(click_lat, click_lon, center_lat, center_lon);
    if (!found || layer_idx < best_layer ||
        (layer_idx == best_layer && dist < best_dist)) {
      found = true;
      best_layer = layer_idx;
      best_dist = dist;
      best_lat = center_lat;
      best_lon = center_lon;
    }
  }

  if (!found)
    return false;

  if (target_lat)
    *target_lat = best_lat;
  if (target_lon)
    *target_lon = best_lon;
  return true;
}

bool nfz_find_nearest_zone_center(const std::vector<DjiNfzZone> &zones,
                                  double ref_lat, double ref_lon,
                                  double *target_lat, double *target_lon) {
  bool found = false;
  double best_dist = 0.0;
  double best_lat = ref_lat;
  double best_lon = ref_lon;

  for (const auto &nfz : zones) {
    double zone_lat = 0.0;
    double zone_lon = 0.0;
    if (!nfz_zone_center(nfz, &zone_lat, &zone_lon))
      continue;

    double dist = distance_m_approx(ref_lat, ref_lon, zone_lat, zone_lon);
    if (!found || dist < best_dist) {
      best_dist = dist;
      best_lat = zone_lat;
      best_lon = zone_lon;
      found = true;
    }
  }

  if (target_lat)
    *target_lat = best_lat;
  if (target_lon)
    *target_lon = best_lon;
  return found;
}

bool nfz_find_reverse_zone_center_in_range(const std::vector<DjiNfzZone> &zones,
                                           double ref_lat, double ref_lon,
                                           double forward_bearing_deg,
                                           double min_dist_m,
                                           double max_dist_m,
                                           double *target_lat,
                                           double *target_lon,
                                           double *out_dist_m,
                                           double *out_nearest_reverse_dist_m) {
  const double backward_deg = std::fmod(forward_bearing_deg + 180.0, 360.0);
  const double rear_cone_half_deg = 3.0;

  bool found_in_range = false;
  double best_dist = std::numeric_limits<double>::max();
  double best_delta = std::numeric_limits<double>::max();
  double best_lat = ref_lat;
  double best_lon = ref_lon;

  bool has_reverse = false;
  double nearest_reverse_dist = std::numeric_limits<double>::max();

  for (const auto &nfz : zones) {
    double zone_lat = 0.0;
    double zone_lon = 0.0;
    if (!nfz_zone_center(nfz, &zone_lat, &zone_lon))
      continue;

    const double dist = distance_m_approx(ref_lat, ref_lon, zone_lat, zone_lon);
    const double zone_bearing = bearing_deg_approx(ref_lat, ref_lon, zone_lat, zone_lon);
    const double delta_back = std::abs(wrap_deg_180(zone_bearing - backward_deg));

    // Narrow reverse-direction gate: keep candidates within +/-3 deg.
    if (delta_back > rear_cone_half_deg)
      continue;

    has_reverse = true;
    if (dist < nearest_reverse_dist)
      nearest_reverse_dist = dist;

    if (dist < min_dist_m || dist > max_dist_m)
      continue;

    // Priority 1: nearest distance in the reverse hemisphere.
    // Priority 2: better bearing match when distance ties.
    if (!found_in_range ||
        dist < best_dist - 1e-9 ||
        (std::abs(dist - best_dist) <= 1e-9 && delta_back < best_delta)) {
      found_in_range = true;
      best_dist = dist;
      best_delta = delta_back;
      best_lat = zone_lat;
      best_lon = zone_lon;
    }
  }

  if (out_nearest_reverse_dist_m) {
    *out_nearest_reverse_dist_m =
        has_reverse ? nearest_reverse_dist : -1.0;
  }

  if (!found_in_range)
    return false;

  if (target_lat)
    *target_lat = best_lat;
  if (target_lon)
    *target_lon = best_lon;
  if (out_dist_m)
    *out_dist_m = best_dist;
  return true;
}
