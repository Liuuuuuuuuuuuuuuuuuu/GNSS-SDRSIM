#include "gui/nfz/nfz_hit_test_utils.h"

#include "gui/geo/geo_io.h"

namespace {

bool nfz_zone_center(const DjiNfzZone &nfz, double *lat, double *lon) {
  if (!lat || !lon)
    return false;

  if (nfz.type == DjiNfzType::CIRCLE) {
    *lat = nfz.center_lat;
    *lon = nfz.center_lon;
    return true;
  }

  if (nfz.type == DjiNfzType::POLYGON && !nfz.polygon.empty()) {
    double sum_lat = 0.0;
    double sum_lon = 0.0;
    for (const auto &pt : nfz.polygon) {
      sum_lat += pt.lat;
      sum_lon += pt.lon;
    }
    *lat = sum_lat / (double)nfz.polygon.size();
    *lon = sum_lon / (double)nfz.polygon.size();
    return true;
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

    if (nfz.type == DjiNfzType::POLYGON) {
      const int poly_size = (int)nfz.polygon.size();
      bool inside = false;
      if (poly_size >= 3) {
        int j = poly_size - 1;
        for (int i = 0; i < poly_size; ++i) {
          double lat_i = nfz.polygon[i].lat;
          double lon_i = nfz.polygon[i].lon;
          double lat_j = nfz.polygon[j].lat;
          double lon_j = nfz.polygon[j].lon;

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
      }

      if (inside && poly_size > 0) {
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
