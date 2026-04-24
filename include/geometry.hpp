/**
 * geometry.hpp — Shared geospatial calculations
 * 
 * Haversine distance, bearing calculations, coordinate validation
 * for RID bridges (WiFi and BLE).
 */

#ifndef GEOMETRY_HPP
#define GEOMETRY_HPP

#include <cmath>

namespace geo {

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

/**
 * Earth's mean radius in meters (WGS-84).
 * Used for Haversine distance calculations.
 */
constexpr double EARTH_RADIUS_M = 6371000.0;

/**
 * Degrees to radians conversion factor.
 */
constexpr double DEG_TO_RAD = M_PI / 180.0;

/**
 * Radians to degrees conversion factor.
 */
constexpr double RAD_TO_DEG = 180.0 / M_PI;

/* ========================================================================== */
/* Core Functions                                                             */
/* ========================================================================== */

/**
 * Convert degrees to radians.
 */
inline double deg2rad(double degrees)
{
    return degrees * DEG_TO_RAD;
}

/**
 * Convert radians to degrees.
 */
inline double rad2deg(double radians)
{
    return radians * RAD_TO_DEG;
}

/**
 * Calculate great-circle distance between two points using Haversine formula.
 * Input: latitude and longitude in decimal degrees.
 * Output: distance in meters.
 * 
 * Haversine formula:
 *   a = sin²(Δlat/2) + cos(lat1) × cos(lat2) × sin²(Δlon/2)
 *   c = 2 × atan2(√a, √(1−a))
 *   d = R × c
 */
inline double haversine_m(double lat1_deg, double lon1_deg,
                          double lat2_deg, double lon2_deg)
{
    const double p1 = deg2rad(lat1_deg);
    const double p2 = deg2rad(lat2_deg);
    const double dp = deg2rad(lat2_deg - lat1_deg);
    const double dl = deg2rad(lon2_deg - lon1_deg);

    const double a = std::sin(dp / 2.0) * std::sin(dp / 2.0) +
                     std::cos(p1) * std::cos(p2) *
                     std::sin(dl / 2.0) * std::sin(dl / 2.0);

    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return EARTH_RADIUS_M * c;
}

/**
 * Calculate initial bearing (azimuth) from point 1 to point 2.
 * Input: latitude and longitude in decimal degrees.
 * Output: bearing in degrees (0° = North, 90° = East, 180° = South, 270° = West).
 * 
 * Formula:
 *   y = sin(Δlon) × cos(lat2)
 *   x = cos(lat1) × sin(lat2) − sin(lat1) × cos(lat2) × cos(Δlon)
 *   θ = atan2(y, x)
 */
inline double bearing_deg(double lat1_deg, double lon1_deg,
                          double lat2_deg, double lon2_deg)
{
    const double p1 = deg2rad(lat1_deg);
    const double p2 = deg2rad(lat2_deg);
    const double dl = deg2rad(lon2_deg - lon1_deg);

    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) -
                     std::sin(p1) * std::cos(p2) * std::cos(dl);

    double bearing = rad2deg(std::atan2(y, x));

    /* Normalize to [0, 360) */
    if (bearing < 0.0) bearing += 360.0;
    return bearing;
}

} /* namespace geo */

#endif /* GEOMETRY_HPP */
