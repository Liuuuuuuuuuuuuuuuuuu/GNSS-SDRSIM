#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../include/odid_common.hpp"
#include "../../../include/bridge_selfcheck.hpp"
#include "../../../include/geometry.hpp"
#include "../../../include/platform.hpp"

namespace {

struct Args {
    std::string iface;
    int udp_port = 39001;
    double obs_lat = 22.758423;
    double obs_lon = 120.337893;
    int min_emit_ms = 1;
    std::vector<std::string> allow_ids;
    std::vector<std::string> block_ids;
    bool self_check_only = false;
    bool mixed_mode = false;
    bool raw_hex = false;
};

struct SeenEntry {
    long long last_emit_ms = 0;
    double last_lat = 0.0;
    double last_lon = 0.0;
};

struct MonitorSeenEntry {
    long long last_emit_ms = 0;
    uint64_t beacon_count = 0;
    uint64_t data_count = 0;
};

struct StatsWindow {
    uint64_t rx = 0;
    uint64_t oui = 0;
    uint64_t odid = 0;
    uint64_t loc = 0;
    uint64_t dropped = 0;
    long long window_start_ms = 0;
};

static constexpr long long kIdleWarnMs = 8000;

static int hop_interval_ms_from_env() {
    const char *raw = std::getenv("BDS_WIFI_RID_HOP_MS");
    if (!raw || !raw[0]) return 220;

    char *end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (!end || *end != '\0' || parsed < 50 || parsed > 5000) {
        return 220;
    }
    return (int)parsed;
}

static void hop_24g_channel_if_due(const std::string &iface,
                                   bool enabled,
                                   int hop_interval_ms,
                                   long long now_ms_value,
                                   long long &last_hop_ms,
                                   size_t &hop_index) {
    static const int kHopChannels24[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    if (!enabled || iface.empty()) return;
    if (last_hop_ms != 0 && (now_ms_value - last_hop_ms) < hop_interval_ms) return;

    last_hop_ms = now_ms_value;
    const int channel = kHopChannels24[hop_index % (sizeof(kHopChannels24) / sizeof(kHopChannels24[0]))];
    hop_index = (hop_index + 1) % (sizeof(kHopChannels24) / sizeof(kHopChannels24[0]));

    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "iw dev %s set channel %d >/dev/null 2>&1",
                  iface.c_str(), channel);
    const int rc = std::system(cmd);
    (void)rc;
}

static std::string lower_trimmed(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static void parse_id_filter_csv(const char *csv, std::vector<std::string> &out) {
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

static bool id_allowed(const std::vector<std::string> &allow_ids,
                       const std::string &device_id,
                       const std::string &remote_id) {
    if (allow_ids.empty()) return true;
    const std::string did = lower_trimmed(device_id);
    const std::string rid = lower_trimmed(remote_id);
    for (const std::string &w : allow_ids) {
        if ((!did.empty() && did == w) || (!rid.empty() && rid == w)) return true;
    }
    return false;
}

static bool id_blocked(const std::vector<std::string> &block_ids,
                       const std::string &device_id,
                       const std::string &remote_id) {
    if (block_ids.empty()) return false;
    const std::string did = lower_trimmed(device_id);
    const std::string rid = lower_trimmed(remote_id);
    for (const std::string &b : block_ids) {
        if ((!did.empty() && did == b) || (!rid.empty() && rid == b)) return true;
    }
    return false;
}

static void usage(const char *argv0) {
    std::fprintf(stderr,
                 "Usage: %s --iface IFACE [--udp-port PORT] [--obs-lat LAT --obs-lon LON] [--min-emit-ms MS]\n"
                 "          [--allow-ids CSV] [--block-ids CSV] [--wifi-mode rid|mixed] [--raw-hex] [--self-check-only]\n"
                 "Defaults: port=39001, obs=(22.758423,120.337893), min-emit-ms=1\n"
                 "          wifi-mode=rid (RID-only)\n"
                 "          wifi-mode=mixed (2.4GHz ch1-13 hop + RID)\n"
                 "          raw-hex=off (or set BDS_WIFI_RID_RAW_HEX=1)\n"
                 "Requires tshark in PATH and capture permissions (usually via sudo).\n",
                 argv0);
}

static bool parse_wifi_mode(const char *raw, bool *mixed_mode) {
    if (!raw || !raw[0] || !mixed_mode) return false;
    const std::string mode = lower_trimmed(std::string(raw));
    if (mode == "rid" || mode == "rid-only" || mode == "rid_only") {
        *mixed_mode = false;
        return true;
    }
    if (mode == "mixed" || mode == "all") {
        *mixed_mode = true;
        return true;
    }
    return false;
}

static bool env_truthy_default_true(const char *raw) {
    if (!raw || !raw[0]) return true;
    const std::string v = lower_trimmed(std::string(raw));
    return !(v == "0" || v == "false" || v == "no" || v == "off");
}

static bool env_truthy_default_false(const char *raw) {
    if (!raw || !raw[0]) return false;
    const std::string v = lower_trimmed(std::string(raw));
    return (v == "1" || v == "true" || v == "yes" || v == "on");
}

static std::string bytes_to_hex_string(const std::vector<uint8_t> &bytes) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t byte : bytes) {
        out.push_back(kHex[(byte >> 4) & 0x0f]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

static bool is_special_rid_target_mac(const std::string &mac) {
    const std::string norm = lower_trimmed(mac);
    return norm == "90:3a:e6:38:bb:92" || norm == "e4:7a:2c:ef:68:17";
}

static std::string shell_escape_single_quotes(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

static std::string read_text_file(const char *path) {
    if (!path || !path[0]) return std::string();
    FILE *fp = std::fopen(path, "r");
    if (!fp) return std::string();

    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp)) {
        out += buf;
    }
    std::fclose(fp);
    return out;
}

static bool parse_args(int argc, char **argv, Args &out) {
    out.raw_hex = env_truthy_default_false(std::getenv("BDS_WIFI_RID_RAW_HEX"));

    const char *mode_env = std::getenv("BDS_WIFI_RID_WIFI_MODE");
    if (mode_env && mode_env[0]) {
        if (!parse_wifi_mode(mode_env, &out.mixed_mode)) {
            std::fprintf(stderr,
                         "[wifi-rid-tshark-bridge] invalid BDS_WIFI_RID_WIFI_MODE=%s (use rid or mixed)\n",
                         mode_env);
            return false;
        }
    } else {
        const char *mixed_env = std::getenv("BDS_WIFI_RID_MIXED_ENABLE");
        out.mixed_mode = env_truthy_default_true(mixed_env);
    }

    parse_id_filter_csv(std::getenv("BDS_RID_BLOCK_IDS"), out.block_ids);
    if (out.block_ids.empty()) {
        parse_id_filter_csv(std::getenv("BDS_WIFI_RID_BLOCK_IDS"), out.block_ids);
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
            out.iface = argv[++i];
        } else if (std::strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            out.udp_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--obs-lat") == 0 && i + 1 < argc) {
            out.obs_lat = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--obs-lon") == 0 && i + 1 < argc) {
            out.obs_lon = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--min-emit-ms") == 0 && i + 1 < argc) {
            out.min_emit_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--allow-ids") == 0 && i + 1 < argc) {
            parse_id_filter_csv(argv[++i], out.allow_ids);
        } else if (std::strcmp(argv[i], "--block-ids") == 0 && i + 1 < argc) {
            out.block_ids.clear();
            parse_id_filter_csv(argv[++i], out.block_ids);
        } else if (std::strcmp(argv[i], "--wifi-mode") == 0 && i + 1 < argc) {
            if (!parse_wifi_mode(argv[++i], &out.mixed_mode)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--raw-hex") == 0) {
            out.raw_hex = true;
        } else if (std::strcmp(argv[i], "--self-check-only") == 0) {
            out.self_check_only = true;
        } else {
            return false;
        }
    }

    if (out.iface.empty() || out.udp_port <= 0 || out.udp_port > 65535) return false;
    if (out.obs_lat < -90.0 || out.obs_lat > 90.0) return false;
    if (out.obs_lon < -180.0 || out.obs_lon > 180.0) return false;
    if (out.min_emit_ms < 1) out.min_emit_ms = 1;
    return true;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_to_bytes(const std::string &in, std::vector<uint8_t> &out) {
    std::string compact;
    compact.reserve(in.size());
    for (char c : in) {
        if (std::isxdigit((unsigned char)c)) compact.push_back(c);
    }
    if (compact.size() < 2 || (compact.size() % 2) != 0) return false;

    out.clear();
    out.reserve(compact.size() / 2);
    for (size_t i = 0; i + 1 < compact.size(); i += 2) {
        const int hi = hex_nibble(compact[i]);
        const int lo = hex_nibble(compact[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return !out.empty();
}

static bool scan_vendor_blob_for_odid(const std::vector<uint8_t> &blob,
                                      odid::OdidData &out) {
    if (blob.size() < 26) return false;

    for (size_t off = 0; off + 26 <= blob.size(); ++off) {
        odid::OdidData cand;
        if (odid::scan_odid_payload(blob.data() + off, blob.size() - off, cand) && cand.has_location) {
            out = cand;
            return true;
        }
    }
    return false;
}

static std::string emit_json(const std::string &mac,
                             double obs_lat,
                             double obs_lon,
                             int rssi_dbm,
                             bool has_rssi,
                             const odid::OdidData &odid_data) {
    const double dist = geo::haversine_m(obs_lat, obs_lon, odid_data.drone_lat, odid_data.drone_lon);
    const double brg  = geo::bearing_deg(obs_lat, obs_lon, odid_data.drone_lat, odid_data.drone_lon);

    char out[2048];
    if (has_rssi) {
        std::snprintf(out, sizeof(out),
                      "{\"detected\":true,\"confidence\":0.92,\"source\":\"wifi-rid\","
                      "\"vendor\":\"odid\",\"model\":\"wireshark\","
                      "\"mac\":\"%s\",\"device_id\":\"%s\","
                      "\"remote_id\":true,\"rid\":true,\"remote_id_id\":\"%s\","
                      "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                      "\"bearing_deg\":%.1f,\"distance_m\":%.1f,"
                      "\"rssi_dbm\":%d,\"has_rssi\":true}"
                      ,
                      mac.c_str(),
                      mac.c_str(),
                      platform::escape_for_json(odid_data.remote_id).c_str(),
                      odid_data.drone_lat, odid_data.drone_lon,
                      brg, dist,
                      rssi_dbm);
    } else {
        std::snprintf(out, sizeof(out),
                      "{\"detected\":true,\"confidence\":0.92,\"source\":\"wifi-rid\","
                      "\"vendor\":\"odid\",\"model\":\"wireshark\","
                      "\"mac\":\"%s\",\"device_id\":\"%s\","
                      "\"remote_id\":true,\"rid\":true,\"remote_id_id\":\"%s\","
                      "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                      "\"bearing_deg\":%.1f,\"distance_m\":%.1f}"
                      ,
                      mac.c_str(),
                      mac.c_str(),
                      platform::escape_for_json(odid_data.remote_id).c_str(),
                      odid_data.drone_lat, odid_data.drone_lon,
                      brg, dist);
    }
    return std::string(out);
}

static std::string emit_bridge_status_json(const StatsWindow &st,
                                           const std::string &iface,
                                           bool mixed_mode,
                                           const char *health_status,
                                           const char *health_detail) {
    const double hr = (st.odid > 0) ? (100.0 * (double)st.loc / (double)st.odid) : 0.0;
    char out[768];
    std::snprintf(out, sizeof(out),
                  "{"
                  "\"bridge_status\":true,"
                  "\"detected\":false,"
                  "\"confidence\":0.0,"
                  "\"source\":\"wifi-rid-status\","
                  "\"bridge\":\"wifi-rid-tshark-bridge\","
                  "\"protocol\":\"WiFi\","
                  "\"iface\":\"%s\","
                  "\"capture_mode\":\"%s\","
                  "\"rx\":%llu,"
                  "\"oui\":%llu,"
                  "\"odid\":%llu,"
                  "\"loc\":%llu,"
                  "\"dropped\":%llu,"
                  "\"hit_rate\":%.1f,"
                  "\"startup_state\":\"running\","
                  "\"self_check\":\"passed\","
                  "\"health_status\":\"%s\","
                  "\"health_detail\":\"%s\""
                  "}",
                  iface.c_str(),
                  mixed_mode ? "mixed" : "rid",
                  (unsigned long long)st.rx,
                  (unsigned long long)st.oui,
                  (unsigned long long)st.odid,
                  (unsigned long long)st.loc,
                  (unsigned long long)st.dropped,
                  hr,
                  health_status,
                  health_detail);
    return std::string(out);
}

static void publish_bridge_status(const StatsWindow &st,
                                  int udp,
                                  const struct sockaddr_in &dst,
                                  const std::string &iface,
                                  bool mixed_mode,
                                  const char *health_status,
                                  const char *health_detail) {
    const std::string status =
        emit_bridge_status_json(st, iface, mixed_mode, health_status, health_detail);
    (void)sendto(udp, status.data(), status.size(), MSG_NOSIGNAL,
                 reinterpret_cast<const struct sockaddr *>(&dst), sizeof(dst));
}

static void flush_stats_if_due(StatsWindow &st,
                               int udp,
                               const struct sockaddr_in &dst,
                               const std::string &iface,
                               bool mixed_mode,
                               long long now_ms_value,
                               const char *health_status,
                               const char *health_detail) {
    if (!st.window_start_ms) {
        st.window_start_ms = now_ms_value;
        return;
    }
    if (now_ms_value - st.window_start_ms < 1000) return;

    if (st.rx > 0 || st.oui > 0 || st.odid > 0 || st.loc > 0 || st.dropped > 0) {
        const double hr = (st.odid > 0) ? (100.0 * (double)st.loc / (double)st.odid) : 0.0;
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] 1s | Rx:%llu OUI:%llu ODID:%llu Loc:%llu Dropped:%llu (hit %.1f%%)\n",
                     (unsigned long long)st.rx,
                     (unsigned long long)st.oui,
                     (unsigned long long)st.odid,
                     (unsigned long long)st.loc,
                     (unsigned long long)st.dropped,
                     hr);
    }

    publish_bridge_status(st, udp, dst, iface, mixed_mode,
                          health_status, health_detail);

    st.rx = st.oui = st.odid = st.loc = st.dropped = 0;
    st.window_start_ms = now_ms_value;
}

static std::string emit_monitor_json(const std::string &mac,
                                     const std::string &essid,
                                     int rssi_dbm,
                                     bool has_rssi,
                                     uint64_t beacon_count,
                                     uint64_t data_count) {
    const std::string oui = (mac.size() >= 8) ? mac.substr(0, 8) : std::string();
    const std::string safe_essid = platform::escape_for_json(essid);
    char out[1024];
    if (has_rssi) {
        std::snprintf(out, sizeof(out),
                      "{"
                      "\"detected\":false,"
                      "\"confidence\":0.10,"
                      "\"source\":\"wifi-beacon\","
                      "\"vendor\":\"wifi\","
                      "\"model\":\"monitor\","
                      "\"mac\":\"%s\","
                      "\"device_id\":\"%s\","
                      "\"oui\":\"%s\","
                      "\"rssi_dbm\":%d,"
                      "\"channel\":0,"
                      "\"security\":\"unknown\","
                      "\"essid\":\"%s\","
                      "\"beacon_count\":%llu,"
                      "\"data_count\":%llu,"
                      "\"remote_id\":false"
                      "}",
                      mac.c_str(),
                      mac.c_str(),
                      oui.c_str(),
                      rssi_dbm,
                      safe_essid.c_str(),
                      (unsigned long long)beacon_count,
                      (unsigned long long)data_count);
    } else {
        std::snprintf(out, sizeof(out),
                      "{"
                      "\"detected\":false,"
                      "\"confidence\":0.10,"
                      "\"source\":\"wifi-beacon\","
                      "\"vendor\":\"wifi\","
                      "\"model\":\"monitor\","
                      "\"mac\":\"%s\","
                      "\"device_id\":\"%s\","
                      "\"oui\":\"%s\","
                      "\"channel\":0,"
                      "\"security\":\"unknown\","
                      "\"essid\":\"%s\","
                      "\"beacon_count\":%llu,"
                      "\"data_count\":%llu,"
                      "\"remote_id\":false"
                      "}",
                      mac.c_str(),
                      mac.c_str(),
                      oui.c_str(),
                      safe_essid.c_str(),
                      (unsigned long long)beacon_count,
                      (unsigned long long)data_count);
    }
    return std::string(out);
}

} // namespace

int main(int argc, char **argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    std::vector<bridge_check::CheckResult> checks;
    checks.push_back(bridge_check::check_udp_port(args.udp_port));
    checks.push_back(bridge_check::check_coordinates(args.obs_lat, args.obs_lon));
    checks.push_back(bridge_check::check_observer_cache_file());
    checks.push_back(bridge_check::check_interface_exists(args.iface));

    const bool tshark_ok = (std::system("command -v tshark >/dev/null 2>&1") == 0);
    if (tshark_ok) {
        checks.push_back(bridge_check::make_result(true, true, "tshark", "tshark found"));
    } else {
        checks.push_back(bridge_check::make_result(true, false, "tshark", "tshark not found in PATH"));
    }

    if (!bridge_check::print_report("wifi-rid-tshark-bridge", checks)) {
        return 2;
    }
    if (args.self_check_only) {
        std::fprintf(stderr, "[wifi-rid-tshark-bridge] self-check only mode: validation complete, exiting before capture\n");
        return 0;
    }
    if (!tshark_ok) {
        std::fprintf(stderr, "[wifi-rid-tshark-bridge] tshark missing, cannot start capture\n");
        return 2;
    }

    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp < 0) {
        std::fprintf(stderr, "[wifi-rid-tshark-bridge] UDP socket failed: %s\n", std::strerror(errno));
        return 2;
    }
    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)args.udp_port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    char tshark_stderr_path[] = "/tmp/wifi-rid-tshark-bridge-stderr-XXXXXX";
    const int tshark_stderr_fd = mkstemp(tshark_stderr_path);
    if (tshark_stderr_fd < 0) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] failed to allocate tshark stderr file: %s\n",
                     std::strerror(errno));
        close(udp);
        return 2;
    }
    close(tshark_stderr_fd);

    char cmd[1400];
    const std::string iface_escaped = shell_escape_single_quotes(args.iface);
    const std::string stderr_path_escaped = shell_escape_single_quotes(tshark_stderr_path);
    std::snprintf(cmd, sizeof(cmd),
                  "tshark -l -n -i %s -Y \"%s\" "
                  "-T fields -E header=n -E separator='|' "
                  "-e frame.time_epoch -e wlan.sa -e radiotap.dbm_antsignal -e wlan.tag.vendor.data -e wlan.fc.type_subtype -e wlan.ssid 2>%s",
                  iface_escaped.c_str(),
                  (args.mixed_mode
                      ? "(wlan.fc.type_subtype == 0x0008) || (wlan.tag.number==221 && wlan.tag.oui==0xfa0bbc)"
                      : "wlan.tag.number==221 && wlan.tag.oui==0xfa0bbc"),
                  stderr_path_escaped.c_str());

    FILE *pp = popen(cmd, "r");
    if (!pp) {
        std::fprintf(stderr, "[wifi-rid-tshark-bridge] failed to start tshark: %s\n", std::strerror(errno));
        std::remove(tshark_stderr_path);
        close(udp);
        return 2;
    }

    std::unordered_map<std::string, SeenEntry> seen;
    std::unordered_map<std::string, MonitorSeenEntry> monitor_seen;
    StatsWindow stats;
    double obs_lat = args.obs_lat;
    double obs_lon = args.obs_lon;
    long long last_obs_refresh_ms = 0;
    const long long start_ms = platform::now_ms();
    long long last_packet_ms = 0;
    long long last_idle_log_ms = 0;
    const int hop_interval_ms = hop_interval_ms_from_env();
    long long last_hop_ms = 0;
    size_t hop_index = 0;

    std::fprintf(stderr,
                 "[wifi-rid-tshark-bridge] startup: iface=%s -> 127.0.0.1:%d observer=(%.6f,%.6f) mode=%s\n",
                 args.iface.c_str(), args.udp_port, obs_lat, obs_lon,
                 args.mixed_mode ? "mixed" : "rid");
    if (args.raw_hex) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] raw hex dump enabled; Vendor IE bytes will be printed to stderr\n");
    }
    if (args.mixed_mode) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] mixed mode enables 2.4GHz channel hop (1-13), interval=%dms\n",
                     hop_interval_ms);
    }

    char line[8192];
    const int tshark_fd = fileno(pp);
    if (tshark_fd < 0) {
        std::fprintf(stderr, "[wifi-rid-tshark-bridge] failed to inspect tshark pipe: %s\n", std::strerror(errno));
        (void)pclose(pp);
        std::remove(tshark_stderr_path);
        close(udp);
        return 2;
    }

    while (true) {
        const long long now_ms_value = platform::now_ms();
        hop_24g_channel_if_due(args.iface,
                               args.mixed_mode,
                               hop_interval_ms,
                               now_ms_value,
                               last_hop_ms,
                               hop_index);
        const long long idle_since_ms = last_packet_ms > 0 ? last_packet_ms : start_ms;
        const bool idle = (now_ms_value - idle_since_ms) >= kIdleWarnMs;
        const char *health_status = idle ? "idle" : "ok";
        const char *health_detail = idle
            ? "bridge idle: no matching Wi-Fi RID packets yet"
            : "tshark capture healthy";
        flush_stats_if_due(stats, udp, dst, args.iface, args.mixed_mode, now_ms_value,
                           health_status, health_detail);

        if (idle && (last_idle_log_ms == 0 || now_ms_value - last_idle_log_ms >= kIdleWarnMs)) {
            std::fprintf(stderr,
                         "[wifi-rid-tshark-bridge] idle: no matching Wi-Fi RID packets for %.1fs; bridge is running.\n",
                         (double)(now_ms_value - idle_since_ms) / 1000.0);
            last_idle_log_ms = now_ms_value;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(tshark_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = args.mixed_mode ? 0 : 1;
        tv.tv_usec = args.mixed_mode ? 200000 : 0;
        const int sel = select(tshark_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr,
                         "[wifi-rid-tshark-bridge] select failed: %s\n",
                         std::strerror(errno));
            break;
        }
        if (sel == 0) {
            continue;
        }
        if (!std::fgets(line, sizeof(line), pp)) {
            break;
        }

        if (now_ms_value - last_obs_refresh_ms > 1000) {
            last_obs_refresh_ms = now_ms_value;
            (void)platform::parse_observer_position(obs_lat, obs_lon);
        }

        last_packet_ms = now_ms_value;
        stats.rx++;

        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty()) continue;

        const size_t p1 = s.find('|');
        const size_t p2 = (p1 == std::string::npos) ? std::string::npos : s.find('|', p1 + 1);
        const size_t p3 = (p2 == std::string::npos) ? std::string::npos : s.find('|', p2 + 1);
        const size_t p4 = (p3 == std::string::npos) ? std::string::npos : s.find('|', p3 + 1);
        const size_t p5 = (p4 == std::string::npos) ? std::string::npos : s.find('|', p4 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
            stats.dropped++;
            continue;
        }

        const std::string mac = lower_trimmed(s.substr(p1 + 1, p2 - (p1 + 1)));
        const std::string rssi_s = lower_trimmed(s.substr(p2 + 1, p3 - (p2 + 1)));
        const std::string vendor_data_multi =
            (p4 == std::string::npos) ? s.substr(p3 + 1) : s.substr(p3 + 1, p4 - (p3 + 1));
        const std::string subtype_s =
            (p4 == std::string::npos)
                ? std::string()
                : lower_trimmed((p5 == std::string::npos)
                                    ? s.substr(p4 + 1)
                                    : s.substr(p4 + 1, p5 - (p4 + 1)));
        const std::string essid =
            (p5 == std::string::npos) ? std::string() : s.substr(p5 + 1);

        int rssi_dbm = 0;
        bool has_rssi = false;
        if (!rssi_s.empty()) {
            rssi_dbm = std::atoi(rssi_s.c_str());
            has_rssi = true;
        }

        if (!vendor_data_multi.empty()) {
            stats.oui++;
        }

        odid::OdidData odid_data;
        bool decoded = false;

        size_t start = 0;
        while (start <= vendor_data_multi.size()) {
            const size_t comma = vendor_data_multi.find(',', start);
            const size_t end = (comma == std::string::npos) ? vendor_data_multi.size() : comma;
            const std::string token = vendor_data_multi.substr(start, end - start);
            std::vector<uint8_t> bytes;
            if (hex_to_bytes(token, bytes)) {
                if (scan_vendor_blob_for_odid(bytes, odid_data)) {
                    if (args.raw_hex && is_special_rid_target_mac(mac)) {
                        std::fprintf(stderr,
                                     "[wifi-rid-tshark-bridge] raw hex mac=%s len=%zu data=%s\n",
                                     mac.empty() ? "(unknown)" : mac.c_str(),
                                     bytes.size(),
                                     bytes_to_hex_string(bytes).c_str());
                    }
                    decoded = true;
                    break;
                }
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }

        if (!decoded || !odid_data.has_location) {
            if (args.mixed_mode && !mac.empty()) {
                long long parsed_subtype = -1;
                if (!subtype_s.empty()) {
                    char *endptr = nullptr;
                    parsed_subtype = std::strtoll(subtype_s.c_str(), &endptr, 0);
                    if (endptr == subtype_s.c_str()) {
                        parsed_subtype = -1;
                    }
                }

                MonitorSeenEntry &m = monitor_seen[mac];
                if (parsed_subtype == 8 || parsed_subtype == 0x0008 || parsed_subtype == 0x0080) {
                    m.beacon_count++;
                } else {
                    m.data_count++;
                }

                if (now_ms_value - m.last_emit_ms >= 1000) {
                    const std::string mon = emit_monitor_json(mac,
                                                              essid,
                                                              rssi_dbm,
                                                              has_rssi,
                                                              m.beacon_count,
                                                              m.data_count);
                    (void)sendto(udp, mon.data(), mon.size(), MSG_NOSIGNAL,
                                 reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
                    m.last_emit_ms = now_ms_value;
                }
            }
            stats.dropped++;
            continue;
        }
        stats.odid++;

        if (id_blocked(args.block_ids, mac, odid_data.remote_id)) {
            continue;
        }
        if (!id_allowed(args.allow_ids, mac, odid_data.remote_id)) {
            continue;
        }

        SeenEntry &entry = seen[mac];
        const bool same_pos = (std::abs(odid_data.drone_lat - entry.last_lat) < odid::COORDINATE_SCALE &&
                               std::abs(odid_data.drone_lon - entry.last_lon) < odid::COORDINATE_SCALE);
        if (same_pos && (now_ms_value - entry.last_emit_ms < args.min_emit_ms)) {
            continue;
        }
        entry.last_emit_ms = now_ms_value;
        entry.last_lat = odid_data.drone_lat;
        entry.last_lon = odid_data.drone_lon;

        const std::string json = emit_json(mac, obs_lat, obs_lon, rssi_dbm, has_rssi, odid_data);
        if (sendto(udp, json.data(), json.size(), MSG_NOSIGNAL,
                   reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst)) < 0) {
            std::fprintf(stderr, "[wifi-rid-tshark-bridge] sendto failed: %s\n", std::strerror(errno));
            continue;
        }

        stats.loc++;
    }

    const int tshark_status = pclose(pp);
    const std::string tshark_stderr = read_text_file(tshark_stderr_path);
    std::remove(tshark_stderr_path);

    if (tshark_status == -1) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark capture wait failed: %s\n",
                     std::strerror(errno));
        close(udp);
        return 2;
    }

    if (WIFEXITED(tshark_status) && WEXITSTATUS(tshark_status) == 0) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark exited normally; capture stream ended unexpectedly"
                     " (rx=%llu, odid=%llu).\n",
                     (unsigned long long)stats.rx,
                     (unsigned long long)stats.odid);
        if (!tshark_stderr.empty()) {
            std::fprintf(stderr,
                         "[wifi-rid-tshark-bridge] tshark stderr follows:\n%s",
                         tshark_stderr.c_str());
        }
        close(udp);
        return 2;
    }

    if (WIFEXITED(tshark_status)) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark exited with status %d.\n",
                     WEXITSTATUS(tshark_status));
    } else if (WIFSIGNALED(tshark_status)) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark terminated by signal %d.\n",
                     WTERMSIG(tshark_status));
    } else {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark ended with unexpected status 0x%x.\n",
                     tshark_status);
    }
    if (!tshark_stderr.empty()) {
        std::fprintf(stderr,
                     "[wifi-rid-tshark-bridge] tshark stderr follows:\n%s",
                     tshark_stderr.c_str());
    }
    close(udp);
    return 2;
}
