#include "gui/nfz/nfz_hit_test_utils.h"

#include "gui/geo/geo_io.h"

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

bool nfz_zone_center(const DjiNfzZone &nfz, double *lat, double *lon) {
  if (!lat || !lon)
    return false;

  if (nfz.type == DjiNfzType::CIRCLE) {
    *lat = nfz.center_lat;
    *lon = nfz.center_lon;
    return true;
  }

  if (nfz.type == DjiNfzType::POLYGON && !nfz.rings.empty()) {
    // 使用外環（rings[0]）計算中心點
    const auto &outer_ring = nfz.rings[0];
    if (!outer_ring.points.empty()) {
      double sum_lat = 0.0;
      double sum_lon = 0.0;
      for (const auto &pt : outer_ring.points) {
        sum_lat += pt.lat;
        sum_lon += pt.lon;
      }
      *lat = sum_lat / (double)outer_ring.points.size();
      *lon = sum_lon / (double)outer_ring.points.size();
      return true;
    }
  }

  return false;
}

} // namespace

bool nfz_pick_target_llh(const std::vector<DjiNfzZone> &zones, double click_lat,
                         double click_lon, double *target_lat,
                         double *target_lon) {
  for (const auto &nfz : zones) {
    if (nfz.type == DjiNfzType::CIRCLE) {
      if (distance_m_approx(click_lat, click_lon, nfz.center_lat,
                            nfz.center_lon) <= nfz.radius_m) {
        if (target_lat)
          *target_lat = nfz.center_lat;
        if (target_lon)
          *target_lon = nfz.center_lon;
        return true;
      }
      continue;
    }

    if (nfz.type == DjiNfzType::POLYGON && !nfz.rings.empty()) {
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
          double center_lat = 0.0;
          double center_lon = 0.0;
          if (nfz_zone_center(nfz, &center_lat, &center_lon)) {
            if (target_lat)
              *target_lat = center_lat;
            if (target_lon)
              *target_lon = center_lon;
          }
          return true;
        }
      }
    }
  }

  return false;
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
