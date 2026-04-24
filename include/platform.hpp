/**
 * platform.hpp — Platform utilities for RID bridges
 * 
 * Timing, file I/O, MAC address formatting, JSON generation helpers.
 */

#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

namespace platform {

/* ========================================================================== */
/* Timing                                                                      */
/* ========================================================================== */

/**
 * Get current time in milliseconds (CLOCK_REALTIME).
 * Returns signed 64-bit value to fit millisecond counts for ~280 million years.
 */
inline long long now_ms()
{
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000LL);
}

/* ========================================================================== */
/* File I/O                                                                    */
/* ========================================================================== */

/**
 * Parse observer position from shared JSON file: /tmp/bds_sim_obs.json
 * Expected format: {...\"lat\": <double>, \"lon\": <double>...}
 * Returns true and updates obs_lat, obs_lon if file is readable and valid.
 * Returns false if file missing, unreadable, or contains invalid coordinates.
 */
inline bool parse_observer_position(double &obs_lat, double &obs_lon)
{
    FILE *f = std::fopen("/tmp/bds_sim_obs.json", "r");
    if (!f) return false;

    char buf[256] = {0};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';

    const char *p_lat = std::strstr(buf, "\"lat\"");
    const char *p_lon = std::strstr(buf, "\"lon\"");
    if (!p_lat || !p_lon) return false;

    p_lat = std::strchr(p_lat, ':');
    p_lon = std::strchr(p_lon, ':');
    if (!p_lat || !p_lon) return false;

    const double lat = std::strtod(p_lat + 1, nullptr);
    const double lon = std::strtod(p_lon + 1, nullptr);

    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;

    obs_lat = lat;
    obs_lon = lon;
    return true;
}

/* ========================================================================== */
/* MAC Address Formatting                                                     */
/* ========================================================================== */

/**
 * Convert 6-byte MAC address to colon-separated string.
 * Example: {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff} → "aa:bb:cc:dd:ee:ff"
 */
inline std::string mac_to_string(const uint8_t mac[6])
{
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

/* ========================================================================== */
/* String Utilities                                                           */
/* ========================================================================== */

/**
 * Safely escape a string for JSON embedding (escaping quotes and backslashes).
 * Input string may contain any characters; output is JSON-safe.
 * Returns escaped string (max length = 2 × input length).
 */
inline std::string escape_for_json(const std::string &input)
{
    std::string out;
    out.reserve(input.size() * 2);
    for (char c : input) {
        if (c == '"') {
            out.append("\\\"");
        } else if (c == '\\') {
            out.append("\\\\");
        } else if (c < 32) {
            /* Control characters: replace with underscore */
            out.push_back('_');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

/**
 * Format double with fixed precision and up to max_len characters.
 * Returns formatted string or truncated if buffer insufficient.
 * Example: format_double(3.14159, 2) → "3.14"
 */
inline std::string format_double(double value, int precision, size_t max_len = 32)
{
    char buf[64];
    const size_t len = std::snprintf(buf, std::min(sizeof(buf), max_len + 1),
                                     "%.*f", precision, value);
    return std::string(buf, len);
}

/* ========================================================================== */
/* JSON Emission Helpers                                                       */
/* ========================================================================== */

/**
 * Emit a JSON field with safety checks.
 * Encodes value as JSON string (numbers unquoted, strings quoted+escaped).
 * Returns JSON field fragment: "key": value (no comma).
 */
inline std::string json_field_double(const char *key, double value)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "\"%s\":%.7f", key, value);
    return std::string(buf);
}

inline std::string json_field_int(const char *key, int64_t value)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "\"%s\":%lld", key, (long long)value);
    return std::string(buf);
}

inline std::string json_field_string(const char *key, const std::string &value)
{
    const std::string escaped = escape_for_json(value);
    char buf[512];
    std::snprintf(buf, sizeof(buf), "\"%s\":\"%s\"", key, escaped.c_str());
    return std::string(buf);
}

inline std::string json_field_bool(const char *key, bool value)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\"%s\":%s", key, value ? "true" : "false");
    return std::string(buf);
}

} /* namespace platform */

#endif /* PLATFORM_HPP */
