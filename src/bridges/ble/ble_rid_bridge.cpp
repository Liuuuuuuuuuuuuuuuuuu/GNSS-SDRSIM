/*
 * ble_rid_bridge.cpp  —  BLE OpenDroneID (Remote ID) bridge
 *
 * Passively scans BLE legacy advertising events, locates Service Data AD
 * type 0x16 carrying UUID 0xFFFA (ASTM F3411 / ODID), decodes the ODID
 * Location message embedded in the payload, and forwards a JSON record over
 * UDP to 127.0.0.1:<port> (default 39001) for gnss-sim to consume.
 *
 * Build:  $(CXX) -O2 -Wall -Wextra -std=c++14 -o bin/ble-rid-bridge \
 *                 src/bridges/ble/ble_rid_bridge.cpp -lbluetooth
 *
 * Usage:  ble-rid-bridge [--hci DEVICE] [--udp-port PORT]
 *                        [--obs-lat LAT --obs-lon LON]
 *
 * Requires root or CAP_NET_RAW (raw HCI socket).
 * If bluetoothd holds the adapter, run: sudo systemctl stop bluetooth
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include "../../../include/odid_common.hpp"
#include "../../../include/bridge_selfcheck.hpp"
#include "../../../include/geometry.hpp"
#include "../../../include/platform.hpp"

/* -------------------------------------------------------------------------- */
/* Configuration and structures                                                */
/* -------------------------------------------------------------------------- */

namespace {

struct Args {
    std::string hci_dev = "hci0";
    int         udp_port = 39001;
    double      obs_lat  = 22.758423;
    double      obs_lon  = 120.337893;
    int         min_emit_ms = 1;
    std::vector<std::string> block_ids;
    bool        self_check_only = false;
};

struct SeenEntry {
    long long last_emit_ms  = 0;
    long long last_seen_ms  = 0;  /* for TTL expiration */
    double    last_lat      = 0.0;
    double    last_lon      = 0.0;
    uint32_t  rejected_jumps = 0;
    double    pending_lat    = 0.0;
    double    pending_lon    = 0.0;
    uint32_t  pending_hits   = 0;
    long long pending_ms     = 0;
};

struct StatsWindow {
    uint64_t  rx      = 0;  /* HCI adv events received            */
    uint64_t  oui     = 0;  /* AD structures with UUID 0xFFFA     */
    uint64_t  odid    = 0;  /* ODID messages found                */
    uint64_t  loc     = 0;  /* Location decoded + emitted via UDP */
    uint64_t  dropped = 0;  /* ODID found but Location failed     */
    long long window_start_ms = 0;
};

struct BleScanProfile {
    uint16_t interval;
    uint16_t window;
    const char *label;
};

static const BleScanProfile kBleScanProfiles[] = {
    {0x00C8, 0x0064, "wide"},
    {0x0060, 0x0030, "balanced"},
    {0x0010, 0x0010, "conservative"},
};

static const char *g_active_ble_scan_profile = "n/a";

static bool env_truthy(const char *name, bool default_value)
{
    const char *v = std::getenv(name);
    if (!v || !v[0]) return default_value;
    if (std::strcmp(v, "0") == 0) return false;
    if (::strcasecmp(v, "false") == 0) return false;
    if (::strcasecmp(v, "no") == 0) return false;
    return true;
}

/* Timing utilities moved to platform.hpp namespace (platform::now_ms) */

/* -------------------------------------------------------------------------- */
/* CLI                                                                         */
/* -------------------------------------------------------------------------- */

static std::string lower_trimmed(std::string s)
{
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static void parse_id_filter_csv(const char *csv, std::vector<std::string> &out)
{
    if (!csv) return;
    const std::string s(csv);
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const size_t end = (comma == std::string::npos) ? s.size() : comma;
        const std::string token = lower_trimmed(s.substr(start, end - start));
        if (!token.empty()) out.push_back(token);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

static bool id_blocked(const std::vector<std::string> &block_ids,
                       const std::string &device_id,
                       const std::string &remote_id)
{
    if (block_ids.empty()) return false;
    const std::string did = lower_trimmed(device_id);
    const std::string rid = lower_trimmed(remote_id);
    for (const std::string &item : block_ids) {
        if ((!did.empty() && did == item) || (!rid.empty() && rid == item)) {
            return true;
        }
    }
    return false;
}

static void usage(const char *argv0)
{
    std::fprintf(stderr,
    "Usage: %s [--hci DEVICE] [--udp-port PORT] [--obs-lat LAT --obs-lon LON] [--min-emit-ms MS] [--block-ids CSV] [--self-check-only]\n"
    "Defaults: hci=hci0, port=39001, lat=22.758423, lon=120.337893, min-emit-ms=1\n"
        "Env: BDS_BLE_RID_BLOCK_IDS=mac_or_rid (or BDS_RID_BLOCK_IDS)\n"
        "Requires root or CAP_NET_RAW. Stop bluetoothd if the adapter is busy.\n",
        argv0);
}

static bool parse_args(int argc, char **argv, Args &out)
{
    parse_id_filter_csv(std::getenv("BDS_BLE_RID_BLOCK_IDS"), out.block_ids);
    if (out.block_ids.empty()) {
        parse_id_filter_csv(std::getenv("BDS_RID_BLOCK_IDS"), out.block_ids);
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hci") == 0 && i + 1 < argc) {
            out.hci_dev = argv[++i];
        } else if (std::strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            out.udp_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--obs-lat") == 0 && i + 1 < argc) {
            out.obs_lat = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--obs-lon") == 0 && i + 1 < argc) {
            out.obs_lon = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--min-emit-ms") == 0 && i + 1 < argc) {
            out.min_emit_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--block-ids") == 0 && i + 1 < argc) {
            out.block_ids.clear();
            parse_id_filter_csv(argv[++i], out.block_ids);
        } else if (std::strcmp(argv[i], "--self-check-only") == 0) {
            out.self_check_only = true;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return false;
        }
    }
    if (out.udp_port <= 0 || out.udp_port > 65535) return false;
    if (out.obs_lat < -90.0  || out.obs_lat > 90.0)  return false;
    if (out.obs_lon < -180.0 || out.obs_lon > 180.0) return false;
    if (out.min_emit_ms < 1) out.min_emit_ms = 1;
    return true;
}

/* Geometry utilities moved to geometry.hpp namespace (geo::*) */

/* All ODID decoders moved to odid_common.hpp namespace (odid::*) */

/* Utility file parsing moved to platform::parse_observer_position */

/* -------------------------------------------------------------------------- */
/* Stats window                                                                */
/* -------------------------------------------------------------------------- */

static std::string emit_bridge_status_json(const StatsWindow &st,
                                           const bridge_check::RuntimeHealth &health,
                                           int dev_id)
{
    const double hr = (st.odid > 0)
                    ? (100.0 * (double)st.loc / (double)st.odid)
                    : 0.0;
    const char *health_status = "ok";
    char health_detail[160];
    std::snprintf(health_detail, sizeof(health_detail), "capture healthy");

    if (health.idle_windows >= 3) {
        health_status = "warn-idle";
        std::snprintf(health_detail, sizeof(health_detail),
                      "no packets for %u consecutive seconds",
                      health.idle_windows);
    } else if (health.no_signature_windows >= 5) {
        health_status = "warn-no-signature";
        std::snprintf(health_detail, sizeof(health_detail),
                      "packets received but no RID signature for %u consecutive seconds",
                      health.no_signature_windows);
    } else if (health.recovery_count > 0) {
        health_status = "ok-recovered";
        std::snprintf(health_detail, sizeof(health_detail),
                      "capture recovered %u time(s)",
                      health.recovery_count);
    }

    char out[1024];
    std::snprintf(out, sizeof(out),
                  "{"
                  "\"bridge_status\":true,"
                  "\"detected\":false,"
                  "\"confidence\":0.0,"
                  "\"source\":\"bt-le-rid-status\","
                  "\"bridge\":\"ble-rid-bridge\","
                  "\"protocol\":\"BLE\","
                  "\"hci\":\"hci%d\","
                  "\"scan_profile\":\"%s\","
                  "\"rx\":%llu,"
                  "\"oui\":%llu,"
                  "\"odid\":%llu,"
                  "\"loc\":%llu,"
                  "\"dropped\":%llu,"
                  "\"hit_rate\":%.1f,"
                  "\"recovery_count\":%u,"
                  "\"idle_windows\":%u,"
                  "\"no_signature_windows\":%u,"
                  "\"last_recovery_ms\":%lld,"
                  "\"startup_state\":\"running\","
                  "\"self_check\":\"passed\","
                  "\"health_status\":\"%s\","
                  "\"health_detail\":\"%s\""
                  "}",
                  dev_id,
                  g_active_ble_scan_profile,
                  (unsigned long long)st.rx,
                  (unsigned long long)st.oui,
                  (unsigned long long)st.odid,
                  (unsigned long long)st.loc,
                  (unsigned long long)st.dropped,
                  hr,
                  health.recovery_count,
                  health.idle_windows,
                  health.no_signature_windows,
                  health.last_recovery_ms,
                  health_status,
                  health_detail);
    return std::string(out);
}

static void flush_stats_if_due(StatsWindow &st,
                               bridge_check::RuntimeHealth &health,
                               int udp,
                               const struct sockaddr_in &dst,
                               int dev_id,
                               long long now)
{
    if (!st.window_start_ms) { st.window_start_ms = now; return; }
    if (now - st.window_start_ms < 1000) return;

    const double hr = (st.odid > 0)
                    ? (100.0 * (double)st.loc / (double)st.odid)
                    : 0.0;
    std::fprintf(stderr,
        "[ble-rid-bridge] 1s | Rx:%llu OUI:%llu ODID:%llu Loc:%llu Dropped:%llu"
        " (hit %.1f%%)\n",
        (unsigned long long)st.rx,     (unsigned long long)st.oui,
        (unsigned long long)st.odid,   (unsigned long long)st.loc,
        (unsigned long long)st.dropped, hr);

    bridge_check::report_runtime_health("ble-rid-bridge", health, st.rx, st.oui);

    const std::string status_json = emit_bridge_status_json(st, health, dev_id);
    const ssize_t status_sent = sendto(udp, status_json.data(), status_json.size(), MSG_NOSIGNAL,
                                       reinterpret_cast<const struct sockaddr *>(&dst),
                                       sizeof(dst));
    if (status_sent < 0) {
        std::fprintf(stderr, "[ble-rid-bridge] status sendto failed: %s\n", std::strerror(errno));
    }

    st.rx = st.oui = st.odid = st.loc = st.dropped = 0;
    st.window_start_ms = now;
}

static int reopen_hci_device(int old_dd, int dev_id, const char *reason)
{
    if (old_dd >= 0) {
        (void)hci_le_set_scan_enable(old_dd, 0x00, 0x00, 250);
        (void)hci_send_cmd(old_dd, OGF_HOST_CTL, OCF_RESET, 0, nullptr);
        hci_close_dev(old_dd);
    }

    usleep(250000);

    const int reopened = hci_open_dev(dev_id);
    if (reopened < 0) {
        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair failed to reopen hci%d after %s: %s\n",
            dev_id,
            reason, std::strerror(errno));
    } else {
        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair reopened hci%d after %s\n",
            dev_id, reason);
    }
    return reopened;
}

static bool configure_le_scan_with_recovery(int &dd, int dev_id)
{
    const uint8_t scan_type = 0x00;  /* passive */
    const uint8_t own_type  = 0x00;  /* public address */
    const uint8_t filter    = 0x00;  /* accept all advertising */

    /* Intel-class controllers often reject the aggressive "wide" setup with EIO.
     * Default to stable profiles first; allow opting into wide explicitly. */
    const bool try_wide = env_truthy("BDS_BLE_RID_TRY_WIDE", false);
    const int profile_order[3] = {1, 2, 0}; /* balanced, conservative, wide */

    for (size_t i = 0; i < sizeof(profile_order) / sizeof(profile_order[0]); ++i) {
        const int idx = profile_order[i];
        if (!try_wide && idx == 0) {
            continue;
        }
        const BleScanProfile &profile = kBleScanProfiles[idx];

        if (i > 0) {
            dd = reopen_hci_device(dd, dev_id, "scan setup retry");
            if (dd < 0) return false;
        }

        if (hci_le_set_scan_parameters(dd,
                                       scan_type,
                                       htobs(profile.interval),
                                       htobs(profile.window),
                                       own_type,
                                       filter,
                                       1000) < 0) {
            std::fprintf(stderr,
                "[ble-rid-bridge] auto-repair could not apply scan profile '%s' parameters: %s\n",
                profile.label, std::strerror(errno));
            continue;
        }

        if (hci_le_set_scan_enable(dd, 0x01, 0x00, 1000) < 0) {
            std::fprintf(stderr,
                "[ble-rid-bridge] auto-repair could not enable scan profile '%s': %s\n",
                profile.label, std::strerror(errno));
            continue;
        }

        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair applied BLE scan profile '%s' (interval=0x%04x window=0x%04x)\n",
            profile.label, profile.interval, profile.window);
        g_active_ble_scan_profile = profile.label;
        return true;
    }

    return false;
}

static bool apply_hci_event_filter(int dd)
{
    struct hci_filter flt;
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
    hci_filter_set_event(EVT_LE_META_EVENT, &flt);
    return setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) == 0;
}

static bool recover_hci_runtime(int &dd,
                                int dev_id,
                                bridge_check::RuntimeHealth &health,
                                const char *reason)
{
    dd = reopen_hci_device(dd, dev_id, reason);
    if (dd < 0) return false;

    if (!configure_le_scan_with_recovery(dd, dev_id)) {
        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair could not restore BLE scan after %s\n",
            reason);
        return false;
    }

    if (!apply_hci_event_filter(dd)) {
        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair could not restore HCI event filter after %s: %s\n",
            reason, std::strerror(errno));
        return false;
    }

    std::fprintf(stderr,
        "[ble-rid-bridge] auto-repair restored capture after %s\n",
        reason);
    bridge_check::note_recovery(health, platform::now_ms());
    return true;
}

/* Utility MAC formatting moved to platform::mac_to_string */

/* JSON emission helper: emit RID detection event as JSON to UDP */
static std::string emit_json(const uint8_t mac[6],
                             double obs_lat, double obs_lon,
                             const odid::OdidData &odid_data)
{
    const double dist = geo::haversine_m(obs_lat, obs_lon, 
                                         odid_data.drone_lat, odid_data.drone_lon);
    const double brg  = geo::bearing_deg(obs_lat, obs_lon, 
                                         odid_data.drone_lat, odid_data.drone_lon);
    
    bool has_op = odid_data.has_operator && 
                  (std::abs(odid_data.operator_lat) > odid::COORDINATE_TOLERANCE || 
                   std::abs(odid_data.operator_lon) > odid::COORDINATE_TOLERANCE);
    
    /* Safe JSON generation with size-checked snprintf */
    char out[2048];
    size_t pos = 0;

    auto append_json = [&](const char *fmt, auto... args) -> bool {
        if (pos >= sizeof(out)) return false;
        const int written = std::snprintf(out + pos, sizeof(out) - pos, fmt, args...);
        if (written < 0) return false;
        const size_t adv = (size_t)written;
        if (adv >= (sizeof(out) - pos)) {
            pos = sizeof(out) - 1;
            out[pos] = '\0';
            return false;
        }
        pos += adv;
        return true;
    };
    
    const std::string mac_str = platform::mac_to_string(mac);
    const std::string safe_id = platform::escape_for_json(odid_data.remote_id);
    
    /* JSON opening and common fields */
    if (!append_json(
               "{\"detected\":true,\"confidence\":0.93,\"source\":\"bt-le-rid\","
               "\"vendor\":\"odid\",\"model\":\"ble\","
               "\"mac\":\"%s\",\"device_id\":\"%s\",\"oui\":\"%02x:%02x:%02x\","
               "\"remote_id\":true,\"rid\":true,\"remote_id_id\":\"%s\","
               "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
               "\"bearing_deg\":%.1f,\"distance_m\":%.1f",
               mac_str.c_str(), mac_str.c_str(), mac[0], mac[1], mac[2],
               safe_id.c_str(),
               odid_data.drone_lat, odid_data.drone_lon, brg, dist)) {
        return std::string(out, pos);
    }
    
    /* Operator fields if available */
    if (has_op) {
        const double op_dist = geo::haversine_m(obs_lat, obs_lon, 
                                               odid_data.operator_lat, odid_data.operator_lon);
        const double op_brg  = geo::bearing_deg(obs_lat, obs_lon, 
                                               odid_data.operator_lat, odid_data.operator_lon);
        if (!append_json(
                  ",\"operator_lat\":%.7f,\"operator_lon\":%.7f,"
                  "\"operator_bearing_deg\":%.1f,\"operator_distance_m\":%.1f",
                  odid_data.operator_lat, odid_data.operator_lon, op_brg, op_dist)) {
            return std::string(out, pos);
        }
    }
    
    /* Vector fields if available */
    if (odid_data.has_vector) {
        if (!append_json(
                  ",\"ground_speed_cms\":%d,\"track_deg\":%.2f",
                  odid_data.ground_speed_cms, odid_data.track_deg)) {
            return std::string(out, pos);
        }
    }
    
    /* Closing flags */
    (void)append_json(
              ",\"has_bearing\":true,\"has_distance\":true,"
              "\"has_operator\":%s,\"has_vector\":%s}",
              has_op ? "true" : "false",
              odid_data.has_vector ? "true" : "false");
    
    return std::string(out, pos);
}

} /* namespace */

/* -------------------------------------------------------------------------- */
/* main                                                                        */
/* -------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    /* ---- Open HCI device ------------------------------------------------- */
    int dev_id = hci_devid(args.hci_dev.c_str());
    if (dev_id < 0) dev_id = hci_get_route(nullptr);

    std::vector<bridge_check::CheckResult> checks;
    checks.push_back(bridge_check::check_udp_port(args.udp_port));
    checks.push_back(bridge_check::check_coordinates(args.obs_lat, args.obs_lon));
    checks.push_back(bridge_check::check_observer_cache_file());
    checks.push_back(bridge_check::check_raw_capture_permissions("raw-capture-permissions"));
    checks.push_back(bridge_check::check_hci_device(args.hci_dev, dev_id));
    if (!bridge_check::print_report("ble-rid-bridge", checks)) {
        return 2;
    }
    if (args.self_check_only) {
        std::fprintf(stderr, "[ble-rid-bridge] self-check only mode: validation complete, exiting before capture\n");
        return 0;
    }

    if (dev_id < 0) {
        std::fprintf(stderr,
            "[ble-rid-bridge] no HCI device '%s' found: %s\n",
            args.hci_dev.c_str(), std::strerror(errno));
        return 2;
    }

    int dd = hci_open_dev(dev_id);
    if (dd < 0) {
        std::fprintf(stderr,
            "[ble-rid-bridge] hci_open_dev(%d) failed: %s\n"
            "[ble-rid-bridge] Try: sudo systemctl stop bluetooth\n",
            dev_id, std::strerror(errno));
        return 2;
    }

    /* ---- LE scan parameters with auto-repair ------------------------------ */
    if (!configure_le_scan_with_recovery(dd, dev_id)) {
        std::fprintf(stderr,
            "[ble-rid-bridge] auto-repair exhausted all BLE scan setup retries\n"
            "[ble-rid-bridge] Try: sudo systemctl stop bluetooth\n");
        hci_close_dev(dd);
        return 2;
    }

    /* ---- HCI event filter: LE Meta events only --------------------------- */
    if (!apply_hci_event_filter(dd)) {
        std::fprintf(stderr,
            "[ble-rid-bridge] setsockopt(HCI_FILTER) failed: %s\n",
            std::strerror(errno));
        hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
        hci_close_dev(dd);
        return 2;
    }

    /* ---- UDP output socket ----------------------------------------------- */
    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp < 0) {
        std::fprintf(stderr,
            "[ble-rid-bridge] UDP socket failed: %s\n", std::strerror(errno));
        hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
        hci_close_dev(dd);
        return 2;
    }
    struct sockaddr_in dst{};
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons((uint16_t)args.udp_port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* ---- State ----------------------------------------------------------- */
    std::unordered_map<std::string, SeenEntry> seen;
    StatsWindow stats;
    bridge_check::RuntimeHealth health;
    double obs_lat = args.obs_lat, obs_lon = args.obs_lon;
    long long last_obs_ms = 0, last_cleanup_ms = 0;
    const long long SEEN_TTL_MS = 600000;  /* 10 minutes */

    std::fprintf(stderr,
        "[ble-rid-bridge] startup: hci%d -> 127.0.0.1:%d, observer=(%.6f,%.6f)\n",
        dev_id, args.udp_port, obs_lat, obs_lon);
    if (!args.block_ids.empty()) {
        std::fprintf(stderr,
            "[ble-rid-bridge] block-ids filter enabled (%zu items)\n",
            args.block_ids.size());
    }

    /* ---- Main read loop -------------------------------------------------- */
    uint8_t buf[HCI_MAX_EVENT_SIZE + 1];

    while (true) {
        const long long tnow = platform::now_ms();
        flush_stats_if_due(stats, health, udp, dst, dev_id, tnow);

        /* Cleanup stale entries every 30 seconds */
        if (tnow - last_cleanup_ms > 30000) {
            last_cleanup_ms = tnow;
            size_t erased = 0;
            for (auto it = seen.begin(); it != seen.end(); ) {
                if (tnow - it->second.last_seen_ms > SEEN_TTL_MS) {
                    it = seen.erase(it);
                    erased++;
                } else {
                    ++it;
                }
            }
            if (erased > 0) {
                std::fprintf(stderr, "[ble-rid-bridge] cleanup: removed %zu stale entries (map now %zu)\n",
                    erased, seen.size());
            }
        }

        /* Refresh observer position from shared file every second */
        if (tnow - last_obs_ms > 1000) {
            last_obs_ms = tnow;
            (void)platform::parse_observer_position(obs_lat, obs_lon);
        }

        ssize_t n = read(dd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EIO || errno == ENODEV || errno == ENETDOWN) {
                std::fprintf(stderr,
                    "[ble-rid-bridge] read hit recoverable error (%s); attempting auto-repair\n",
                    std::strerror(errno));
                if (recover_hci_runtime(dd, dev_id, health, "runtime read recovery")) {
                    continue;
                }
            }
            std::fprintf(stderr,
                "[ble-rid-bridge] read error: %s\n", std::strerror(errno));
            break;
        }
        if (n < 4) continue;

        /*
         * HCI Event Packet layout:
         *   buf[0] = packet type  (0x04 = HCI_EVENT_PKT)
         *   buf[1] = event code   (0x3E = EVT_LE_META_EVENT)
         *   buf[2] = param length
         *   buf[3] = LE subevent  (0x02 = EVT_LE_ADVERTISING_REPORT)
         *   buf[4] = num_reports
         *   buf[5..] = report data
         */
        if (buf[0] != HCI_EVENT_PKT)      continue;
        if (buf[1] != EVT_LE_META_EVENT)  continue;

        const uint8_t param_len = buf[2];
        if (param_len < 2 || (ssize_t)(3 + param_len) > n) continue;

        const uint8_t subevent   = buf[3];
        const uint8_t num_reports = buf[4];
        if (subevent != EVT_LE_ADVERTISING_REPORT) continue;
        if (num_reports == 0) continue;

        /* Walk the advertising reports */
        size_t off = 5; /* offset into buf[] */
        for (uint8_t r = 0; r < num_reports; ++r) {
            /*
             * Report layout:
             *   [0]    event_type  (1 byte)
             *   [1]    address_type (1 byte)
             *   [2..7] address     (6 bytes, little-endian)
             *   [8]    data_length (1 byte)
             *   [9..9+data_length-1] advertising data
             *   [9+data_length]    RSSI (1 byte, signed)
             */
            if (off + 9 > (size_t)n) break;
            const uint8_t data_len = buf[off + 8];
            if (off + 9 + data_len + 1 > (size_t)n) break;

            const uint8_t *addr_le  = buf + off + 2;  /* 6 bytes, LSB first  */
            const uint8_t *adv_data = buf + off + 9;
            off += 9 + (size_t)data_len + 1;           /* advance past RSSI   */

            stats.rx++;

            /* ---- Scan AD structures -------------------------------------- */
            odid::OdidData odid_data;
            bool    found_uuid = false;

            size_t p = 0;
            while (p < data_len) {
                const uint8_t ad_len  = adv_data[p];
                if (ad_len == 0) break;
                if (p + 1 + (size_t)ad_len > data_len) break;

                const uint8_t ad_type = adv_data[p + 1];
                const uint8_t *ad_val = adv_data + p + 2;
                const size_t   ad_vlen = (size_t)ad_len - 1;

                /* AD type 0x16 = Service Data – 16-bit UUID */
                if (ad_type == 0x16 && ad_vlen >= 2) {
                    const uint16_t uuid = (uint16_t)ad_val[0]
                                        | ((uint16_t)ad_val[1] << 8);
                    if (uuid == 0xFFFA) {
                        found_uuid = true;
                        stats.oui++;
                        const uint8_t *payload = ad_val + 2;
                        const size_t   plen    = ad_vlen - 2;
                        odid::scan_odid_payload(payload, plen, odid_data);
                    }
                }
                p += 1 + (size_t)ad_len;
            }

            if (!found_uuid) continue;

            stats.odid++;
            if (!odid_data.has_location) {
                stats.dropped++;
                continue;
            }

            /* Build canonical MAC (HCI addr bytes are LSB-first) */
            uint8_t mac[6];
            for (int i = 0; i < 6; ++i) mac[i] = addr_le[5 - i];
            const std::string macs = platform::mac_to_string(mac);

            if (id_blocked(args.block_ids, macs, odid_data.remote_id)) {
                continue;
            }

            /* Rate-limit: suppress identical position if below configured emit interval */
            SeenEntry &entry = seen[macs];
            entry.last_seen_ms = tnow;  /* update TTL */

            if (entry.last_emit_ms > 0) {
                const double dt_s = std::max(0.001, (tnow - entry.last_emit_ms) / 1000.0);
                if (dt_s <= 20.0) {
                    const double jump_m = geo::haversine_m(entry.last_lat, entry.last_lon,
                                                           odid_data.drone_lat, odid_data.drone_lon);
                    const double max_jump_m = std::max(20.0, 45.0 * dt_s + 10.0);
                    if (jump_m > max_jump_m) {
                        bool confirmed_reloc = false;
                        if (entry.pending_hits > 0 && (tnow - entry.pending_ms) <= 5000) {
                            const double pending_delta = geo::haversine_m(entry.pending_lat, entry.pending_lon,
                                                                           odid_data.drone_lat, odid_data.drone_lon);
                            if (pending_delta <= 20.0) {
                                entry.pending_lat = odid_data.drone_lat;
                                entry.pending_lon = odid_data.drone_lon;
                                entry.pending_hits++;
                                entry.pending_ms = tnow;
                                if (entry.pending_hits >= 2) {
                                    confirmed_reloc = true;
                                    entry.pending_hits = 0;
                                }
                            } else {
                                entry.pending_lat = odid_data.drone_lat;
                                entry.pending_lon = odid_data.drone_lon;
                                entry.pending_hits = 1;
                                entry.pending_ms = tnow;
                            }
                        } else {
                            entry.pending_lat = odid_data.drone_lat;
                            entry.pending_lon = odid_data.drone_lon;
                            entry.pending_hits = 1;
                            entry.pending_ms = tnow;
                        }

                        if (!confirmed_reloc) {
                            entry.rejected_jumps++;
                            if (entry.rejected_jumps <= 3 || (entry.rejected_jumps % 20) == 0) {
                                std::fprintf(stderr,
                                    "[ble-rid-bridge] reject jump %s jump=%.1fm dt=%.2fs prev=(%.7f,%.7f) now=(%.7f,%.7f)\n",
                                    macs.c_str(), jump_m, dt_s,
                                    entry.last_lat, entry.last_lon,
                                    odid_data.drone_lat, odid_data.drone_lon);
                            }
                            stats.dropped++;
                            continue;
                        }
                        std::fprintf(stderr,
                            "[ble-rid-bridge] accept relocation %s after %u consistent samples\n",
                            macs.c_str(), entry.pending_hits);
                    }
                }
            }

            const bool same_pos = (std::abs(odid_data.drone_lat - entry.last_lat) < odid::COORDINATE_SCALE &&
                                   std::abs(odid_data.drone_lon - entry.last_lon) < odid::COORDINATE_SCALE);
            if (same_pos && (tnow - entry.last_emit_ms < args.min_emit_ms)) continue;
            entry.last_emit_ms = tnow;
            entry.last_lat     = odid_data.drone_lat;
            entry.last_lon     = odid_data.drone_lon;

            /* Emit UDP JSON */
            const std::string json = emit_json(mac, obs_lat, obs_lon, odid_data);
            if (sendto(udp, json.data(), json.size(), MSG_NOSIGNAL,
                       reinterpret_cast<struct sockaddr *>(&dst),
                       sizeof(dst)) < 0) {
                std::fprintf(stderr,
                    "[ble-rid-bridge] sendto(%.128s...) failed: %s\n",
                    json.c_str(), std::strerror(errno));
                continue;
            }

            stats.loc++;
            std::fprintf(stderr,
                "[ble-rid-bridge] src_mac=%s id=%s lat=%.7f lon=%.7f dist=%.1fm"
                " brg=%.1f%s%s\n",
                macs.c_str(), 
                odid_data.remote_id.empty() ? "(none)" : odid_data.remote_id.c_str(),
                odid_data.drone_lat, odid_data.drone_lon,
                geo::haversine_m(obs_lat, obs_lon, odid_data.drone_lat, odid_data.drone_lon),
                geo::bearing_deg(obs_lat, obs_lon, odid_data.drone_lat, odid_data.drone_lon),
                odid_data.has_operator ? " op_loc" : "",
                odid_data.has_vector ? " vel" : "");
        }
    }

    /* ---- Cleanup --------------------------------------------------------- */
    hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
    hci_close_dev(dd);
    close(udp);
    return 0;
}
