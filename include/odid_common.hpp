/**
 * odid_common.hpp — Shared OpenDroneID (ASTM F3411) utilities
 * 
 * Unified ODID message decoding, validation, and data structures
 * used by both WiFi and BLE RID bridges.
 */

#ifndef ODID_COMMON_HPP
#define ODID_COMMON_HPP

#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>

namespace odid {

/* ========================================================================== */
/* Constants & Configuration                                                  */
/* ========================================================================== */

constexpr size_t MESSAGE_LENGTH = 25;          /* Fixed ODID message length */
constexpr size_t MAX_REMOTE_ID_LENGTH = 20;    /* Max ASCII ID field length */
constexpr int16_t INVALID_ALTITUDE = -32768;   /* Sentinel for missing altitude */

/* Coordinate validation bounds */
constexpr double MIN_LATITUDE = -90.0;
constexpr double MAX_LATITUDE = 90.0;
constexpr double MIN_LONGITUDE = -180.0;
constexpr double MAX_LONGITUDE = 180.0;
constexpr double COORDINATE_TOLERANCE = 1e-9;  /* Min non-zero coordinate */
constexpr double COORDINATE_SCALE = 1e-7;       /* GPS scale factor */
constexpr double TRACK_SCALE = 0.01;             /* Track direction scale */

/* Type identifiers (high nibble of byte 0) */
constexpr uint8_t MSG_TYPE_BASIC_ID = 0;
constexpr uint8_t MSG_TYPE_LOCATION = 1;
constexpr uint8_t MSG_TYPE_VECTOR = 2;
constexpr uint8_t MSG_TYPE_SYSTEM = 4;

/* ========================================================================== */
/* Data Structures                                                             */
/* ========================================================================== */

/**
 * Unified ODID message payload after decoding.
 * Set has_* flags to indicate which fields are valid.
 */
struct OdidData {
    /* Type 1: Location (drone position) */
    double drone_lat = 0.0;
    double drone_lon = 0.0;
    bool has_location = false;

    /* Type 2: Vector (velocity) */
    int16_t ground_speed_cms = 0;
    double track_deg = 0.0;
    bool has_vector = false;

    /* Type 4: System (operator position) */
    double operator_lat = 0.0;
    double operator_lon = 0.0;
    int16_t operator_alt = INVALID_ALTITUDE;
    bool has_operator = false;

    /* Type 0: Basic ID (device identifier) */
    std::string remote_id;
    bool has_basic_id = false;
};

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

/**
 * Extract message type from high nibble of first byte.
 * Returns 0-15 (high nibble) or 255 if msg is too short.
 */
inline uint8_t message_type(const uint8_t *msg, size_t len)
{
    if (len < 1) return 255;
    return (msg[0] >> 4) & 0x0F;
}

/**
 * Read 32-bit little-endian signed integer.
 * Does NOT perform bounds checking; caller must ensure at least 4 bytes available.
 */
inline int32_t le_i32(const uint8_t *p)
{
    return (int32_t)p[0]
         | ((int32_t)p[1] << 8)
         | ((int32_t)p[2] << 16)
         | ((int32_t)p[3] << 24);
}

/**
 * Read 16-bit little-endian signed integer.
 */
inline int16_t le_i16(const uint8_t *p)
{
    return (int16_t)p[0] | ((int16_t)p[1] << 8);
}

/**
 * Check if a coordinate is valid (within bounds and non-zero).
 */
inline bool is_valid_latitude(double lat)
{
    return lat >= MIN_LATITUDE && lat <= MAX_LATITUDE
        && (std::abs(lat) >= COORDINATE_TOLERANCE);
}

inline bool is_valid_longitude(double lon)
{
    return lon >= MIN_LONGITUDE && lon <= MAX_LONGITUDE
        && (std::abs(lon) >= COORDINATE_TOLERANCE);
}

inline bool is_valid_position(double lat, double lon)
{
    return is_valid_latitude(lat) && is_valid_longitude(lon);
}

/* ========================================================================== */
/* Message Decoders                                                            */
/* ========================================================================== */

/**
 * Decode Type 1 (Location message).
 * Extracts drone latitude (msg[5..8]) and longitude (msg[9..12]) at scale 1e-7.
 * Returns true and updates lat_deg, lon_deg if successful.
 * Returns false if message is malformed or coordinates are invalid.
 */
inline bool decode_type1_location(const uint8_t *msg, size_t len,
                                   double &lat_deg, double &lon_deg)
{
    if (len < MESSAGE_LENGTH) return false;
    if (message_type(msg, len) != MSG_TYPE_LOCATION) return false;

    const int32_t lat_raw = le_i32(msg + 5);
    const int32_t lon_raw = le_i32(msg + 9);
    const double lat = (double)lat_raw * COORDINATE_SCALE;
    const double lon = (double)lon_raw * COORDINATE_SCALE;

    if (!is_valid_position(lat, lon)) return false;

    lat_deg = lat;
    lon_deg = lon;
    return true;
}

/**
 * Decode Type 4 (System/Operator message).
 * Extracts operator latitude (msg[5..8]), longitude (msg[9..12]), altitude (msg[13..14]).
 * Returns true and updates outputs if successful.
 */
inline bool decode_type4_system(const uint8_t *msg, size_t len,
                                 double &op_lat_deg, double &op_lon_deg,
                                 int16_t &altitude_m)
{
    if (len < MESSAGE_LENGTH) return false;
    if (message_type(msg, len) != MSG_TYPE_SYSTEM) return false;

    const int32_t op_lat_raw = le_i32(msg + 5);
    const int32_t op_lon_raw = le_i32(msg + 9);
    const double op_lat = (double)op_lat_raw * COORDINATE_SCALE;
    const double op_lon = (double)op_lon_raw * COORDINATE_SCALE;

    if (!is_valid_position(op_lat, op_lon)) return false;

    const int16_t alt_raw = le_i16(msg + 13);
    op_lat_deg = op_lat;
    op_lon_deg = op_lon;
    altitude_m = alt_raw;
    return true;
}

/**
 * Decode Type 2 (Vector/Velocity message).
 * Extracts ground speed (msg[2..3]) in cm/s and track direction (msg[4..5]) at scale 0.01°.
 * Returns true and updates outputs if successful.
 */
inline bool decode_type2_vector(const uint8_t *msg, size_t len,
                                 int16_t &ground_speed_cms, double &track_deg)
{
    if (len < MESSAGE_LENGTH) return false;
    if (message_type(msg, len) != MSG_TYPE_VECTOR) return false;

    const int16_t gs_raw = le_i16(msg + 2);
    const int16_t track_raw = le_i16(msg + 4);

    ground_speed_cms = gs_raw;
    track_deg = (double)track_raw * TRACK_SCALE;
    return true;
}

/**
 * Decode Type 0 (Basic ID message).
 * Attempts ASCII extraction from standard offsets with heuristic selection.
 * Returns true and updates out_id if a valid ID is found (min 3 chars).
 */
inline bool decode_type0_basic_id(const uint8_t *msg, size_t len,
                                   std::string &out_id)
{
    if (len < MESSAGE_LENGTH) return false;
    if (message_type(msg, len) != MSG_TYPE_BASIC_ID) return false;

    auto extract_ascii = [](const uint8_t *p, size_t n) -> std::string {
        std::string s;
        s.reserve(n);
        for (size_t i = 0; i < n && p[i] != 0; ++i) {
            const unsigned char c = p[i];
            if (c >= 32 && c <= 126) {
                if (c != '"' && c != '\\') s.push_back((char)c);
            } else break;
        }
        while (!s.empty() && (unsigned char)s.back() <= 32) s.pop_back();
        return s;
    };

    const std::string cand1 = extract_ascii(msg + 2, MAX_REMOTE_ID_LENGTH);
    const std::string cand2 = extract_ascii(msg + 5, MAX_REMOTE_ID_LENGTH);
    const std::string &best = (cand2.size() > cand1.size()) ? cand2 : cand1;

    if (best.size() < 3) return false;
    out_id = best;
    return true;
}

/**
 * Scan a binary blob for ODID messages of all types.
 * Loads any valid messages found into odid_data.
 * Assumes blob = [1-byte counter] + [N × 25-byte ODID messages].
 * Returns true if at least Type 1 (Location) was decoded successfully.
 */
inline bool scan_odid_payload(const uint8_t *payload, size_t plen,
                               OdidData &out_data)
{
    if (plen < 26) return false;  /* Counter + at least 1 message */

    const uint8_t *msgs = payload + 1;
    const size_t msgcount = (plen - 1) / MESSAGE_LENGTH;

    for (size_t i = 0; i < msgcount; ++i) {
        const uint8_t *msg = msgs + (i * MESSAGE_LENGTH);
        const uint8_t type = message_type(msg, MESSAGE_LENGTH);

        switch (type) {
            case MSG_TYPE_BASIC_ID:
                decode_type0_basic_id(msg, MESSAGE_LENGTH, out_data.remote_id);
                out_data.has_basic_id = true;
                break;
            case MSG_TYPE_LOCATION:
                if (decode_type1_location(msg, MESSAGE_LENGTH,
                                         out_data.drone_lat, out_data.drone_lon)) {
                    out_data.has_location = true;
                }
                break;
            case MSG_TYPE_VECTOR:
                if (decode_type2_vector(msg, MESSAGE_LENGTH,
                                       out_data.ground_speed_cms, out_data.track_deg)) {
                    out_data.has_vector = true;
                }
                break;
            case MSG_TYPE_SYSTEM:
                if (decode_type4_system(msg, MESSAGE_LENGTH,
                                       out_data.operator_lat, out_data.operator_lon,
                                       out_data.operator_alt)) {
                    out_data.has_operator = true;
                }
                break;
            default:
                break;
        }
    }

    return out_data.has_location;
}

} /* namespace odid */

#endif /* ODID_COMMON_HPP */
