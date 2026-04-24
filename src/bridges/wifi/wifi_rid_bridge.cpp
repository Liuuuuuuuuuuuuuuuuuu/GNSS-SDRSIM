#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

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
    int hop_interval_ms = 200;
    std::vector<std::string> allow_ids;
    std::vector<std::string> block_ids;
    bool self_check_only = false;
    std::string role;         /* --role: "searcher" or "tracker" (empty = auto-detect) */
    std::string channels_csv; /* --channels: override BDS_WIFI_RID_CHANNELS (CSV) */
};

struct SeenEntry {
    long long last_emit_ms = 0;
    double last_lat = 0.0;
    double last_lon = 0.0;
    uint32_t rejected_jumps = 0;
    double pending_lat = 0.0;
    double pending_lon = 0.0;
    uint32_t pending_hits = 0;
    long long pending_ms = 0;
};

struct MonitorApEntry {
    long long last_emit_ms = 0;
    int signal_dbm = 0;
    int channel = 0;
    uint64_t beacon_count = 0;
    uint64_t data_count = 0;
    std::string security;
    std::string essid;
};

struct StatsWindow {
    uint64_t rx = 0;
    uint64_t oui = 0;
    uint64_t odid = 0;
    uint64_t loc = 0;
    uint64_t dropped = 0;
    uint64_t searcher_hit = 0;
    uint64_t tracker_followup_hit = 0;
    uint64_t tracker_lock_event = 0;
    long long window_start_ms = 0;
};

const int kHopChannels24[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
const int kHopChannels5[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
std::vector<int> g_hop_channels;
int g_last_drone_channel = -1;  /* Last detected drone channel (for weighted hopping) */
bool g_weighted_hop_external = false;  /* Enable weighted hopping for external adapter */
bool g_fiveghz_interleave = false;  /* Interleave 5GHz channels for internal adapter */
bool g_debug_frames  = false;  /* --debug-frames:   print every Vendor IE with hex dump */
bool g_raw_hex_special_only = false;  /* --raw-hex/BDS_WIFI_RID_RAW_HEX: print decoded RID hex for special MACs only */
bool g_any_vendor    = false;  /* --any-vendor:     count any eid=221 as ODID OUI */
bool g_dump_all_ies  = false;  /* --dump-all-ies:   print ALL IEs (eid+len) in each management frame */

enum BridgeRole { ROLE_STANDARD, ROLE_SEARCHER, ROLE_TRACKER };
static BridgeRole g_bridge_role = ROLE_STANDARD;

struct TrackerChannelRecord {
    int channel = -1;
    long long ts_ms = 0;
    int rssi_dbm = -127;  /* -127 means unknown */
    std::string iface;
    bool valid = false;
};

/* IPC file: searchers write detected drone channel; tracker reads and locks to it */
static const char kTrackerChannelFile[] = "/tmp/bds_rid_tracker_ch";
static bool g_tracker_followup_pending = false;
static long long g_tracker_followup_deadline_ms = 0;

int env_int_clamped(const char *name, int default_value, int min_value, int max_value) {
    const char *raw = std::getenv(name);
    if (!raw || !raw[0]) return default_value;
    char *end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    if (!end || *end != '\0') return default_value;
    if (v < min_value || v > max_value) return default_value;
    return (int)v;
}

static TrackerChannelRecord read_tracker_channel_record(long long now_ms, int stale_ms) {
    TrackerChannelRecord rec;
    FILE *f = std::fopen(kTrackerChannelFile, "r");
    if (!f) return rec;

    char line[256] = {0};
    if (!std::fgets(line, sizeof(line), f)) {
        std::fclose(f);
        return rec;
    }
    std::fclose(f);

    int channel = 0;
    long long ts = 0;
    int rssi = -127;
    char iface[64] = {0};
    const int n = std::sscanf(line, "%d:%lld:%d:%63s", &channel, &ts, &rssi, iface);
    if (n < 2 || channel < 1 || channel > 13) return rec;
    if (now_ms - ts > (long long)stale_ms) return rec;

    rec.channel = channel;
    rec.ts_ms = ts;
    rec.rssi_dbm = (n >= 3) ? rssi : -127;
    rec.iface = (n >= 4) ? std::string(iface) : std::string();
    rec.valid = true;
    return rec;
}

static void notify_tracker_channel(int channel, int rssi_dbm, const std::string &iface) {
    if (channel < 1 || channel > 13) return;

    const long long now_ms = platform::now_ms();
    const int min_rssi_dbm = env_int_clamped("BDS_WIFI_RID_IPC_MIN_RSSI", -82, -120, -20);
    const int switch_delta_db = env_int_clamped("BDS_WIFI_RID_IPC_SWITCH_DELTA_DB", 6, 0, 30);
    const int arb_window_ms = env_int_clamped("BDS_WIFI_RID_IPC_ARB_WINDOW_MS", 300, 50, 2000);

    if (rssi_dbm != -127 && rssi_dbm < min_rssi_dbm) {
        return;
    }

    const TrackerChannelRecord current = read_tracker_channel_record(now_ms, arb_window_ms);
    if (current.valid && current.channel != channel &&
        current.rssi_dbm != -127 && rssi_dbm != -127) {
        /* Only switch to another channel when the new candidate is clearly stronger. */
        if (rssi_dbm < current.rssi_dbm + switch_delta_db) {
            return;
        }
    }

    char tmppath[128];
    std::snprintf(tmppath, sizeof(tmppath), "%s.tmp", kTrackerChannelFile);
    FILE *f = std::fopen(tmppath, "w");
    if (!f) return;
    std::fprintf(f, "%d:%lld:%d:%s\n", channel, now_ms, rssi_dbm, iface.c_str());
    std::fclose(f);
    std::rename(tmppath, kTrackerChannelFile);
}

std::string bytes_to_hex_string(const unsigned char *data, size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char byte = data[i];
        out.push_back(kHex[(byte >> 4) & 0x0f]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

bool is_special_rid_target_mac(std::string mac) {
    while (!mac.empty() && std::isspace((unsigned char)mac.front())) mac.erase(mac.begin());
    while (!mac.empty() && std::isspace((unsigned char)mac.back())) mac.pop_back();
    std::transform(mac.begin(), mac.end(), mac.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return mac == "90:3a:e6:38:bb:92" || mac == "e4:7a:2c:ef:68:17";
}

bool parse_channel_list_csv(const char *csv, std::vector<int> &out) {
    if (!csv || !csv[0]) return false;
    std::string s(csv);
    size_t start = 0;
    while (start <= s.size()) {
        const size_t comma = s.find(',', start);
        const size_t end = (comma == std::string::npos) ? s.size() : comma;
        std::string tok = s.substr(start, end - start);
        while (!tok.empty() && std::isspace((unsigned char)tok.front())) tok.erase(tok.begin());
        while (!tok.empty() && std::isspace((unsigned char)tok.back())) tok.pop_back();
        if (!tok.empty()) {
            char *ep = nullptr;
            long ch = std::strtol(tok.c_str(), &ep, 10);
            if (ep && *ep == '\0' && ch >= 1 && ch <= 196) out.push_back((int)ch);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return !out.empty();
}

int open_packet_socket_with_recovery(const std::string &iface,
                                     const char *reason,
                                     int retry_count = 3) {
    for (int attempt = 1; attempt <= retry_count; ++attempt) {
        const int ifindex = if_nametoindex(iface.c_str());
        if (ifindex == 0) {
            std::fprintf(stderr,
                         "[wifi-rid-bridge] auto-repair could not resolve interface %s during %s\n",
                         iface.c_str(), reason);
            return -1;
        }

        const int rx = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (rx < 0) {
            std::fprintf(stderr,
                         "[wifi-rid-bridge] auto-repair socket attempt %d/%d during %s failed: %s\n",
                         attempt, retry_count, reason, std::strerror(errno));
            usleep(200000);
            continue;
        }

        const int rcvbuf = 4 * 1024 * 1024;
        (void)setsockopt(rx, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

        struct sockaddr_ll sll {};
        sll.sll_family = AF_PACKET;
        sll.sll_protocol = htons(ETH_P_ALL);
        sll.sll_ifindex = ifindex;
        if (bind(rx, (struct sockaddr *)&sll, sizeof(sll)) == 0) {
            if (attempt > 1) {
                std::fprintf(stderr,
                             "[wifi-rid-bridge] auto-repair restored packet capture on %s after %s\n",
                             iface.c_str(), reason);
            }
            return rx;
        }

        std::fprintf(stderr,
                     "[wifi-rid-bridge] auto-repair bind attempt %d/%d on %s during %s failed: %s\n",
                     attempt, retry_count, iface.c_str(), reason, std::strerror(errno));
        close(rx);
        usleep(200000);
    }

    return -1;
}

void init_hop_channels_from_env() {
    g_hop_channels.clear();

    const char *csv = std::getenv("BDS_WIFI_RID_CHANNELS");
    if (parse_channel_list_csv(csv, g_hop_channels)) return;

    const char *band = std::getenv("BDS_WIFI_RID_BAND");
    std::string b = band ? band : "";
    while (!b.empty() && std::isspace((unsigned char)b.front())) b.erase(b.begin());
    while (!b.empty() && std::isspace((unsigned char)b.back())) b.pop_back();
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    if (b == "5g" || b == "5ghz") {
        g_hop_channels.assign(kHopChannels5,
                              kHopChannels5 + sizeof(kHopChannels5) / sizeof(kHopChannels5[0]));
    } else {
        g_hop_channels.assign(kHopChannels24,
                              kHopChannels24 + sizeof(kHopChannels24) / sizeof(kHopChannels24[0]));
    }
}

std::string hop_channels_to_string() {
    if (g_hop_channels.empty()) return "(none)";
    std::string out;
    for (size_t i = 0; i < g_hop_channels.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(g_hop_channels[i]);
    }
    return out;
}

/* Generate interleaved channel sequence for weighted hopping
   If drone_channel > 0 and weighted mode enabled: 1, drone_ch, 2, drone_ch, 3, drone_ch...
   Otherwise: use original g_hop_channels
   This is typically for external USB adapter to prioritize known drone channels */
std::vector<int> generate_weighted_channels(int drone_channel) {
    std::vector<int> result;
    
    /* If no drone detected yet or weighted mode disabled, use standard hopping */
    if (drone_channel <= 0) {
        result.insert(result.end(), g_hop_channels.begin(), g_hop_channels.end());
        return result;
    }
    
    /* Weighted hopping: interleave drone_channel with other channels */
    /* Pattern: ch1, drone, ch2, drone, ch3, drone, ... */
    for (const int ch : g_hop_channels) {
        if (ch != drone_channel) {
            result.push_back(ch);
            result.push_back(drone_channel);
        }
    }
    /* Ensure we end with drone channel */
    if (!result.empty() && result.back() != drone_channel) {
        result.push_back(drone_channel);
    }
    return result;
}

/* Generate fixed channel sequence for internal NIC after drone detected
   Once drone_channel is detected, internal NIC stays on that channel only
   Pattern: just repeat drone_channel */
std::vector<int> generate_fixed_drone_channel(int drone_channel) {
    std::vector<int> result;
    
    /* If no drone channel detected yet, return empty (no scanning) */
    if (drone_channel <= 0) {
        return result;
    }
    
    /* Drone channel detected: just stay on it (repeat for multiple hops) */
    for (int i = 0; i < 10; ++i) {  /* Repeat drone channel 10 times per cycle */
        result.push_back(drone_channel);
    }
    return result;
}

/* Generate 5GHz interleaved sequence for internal NIC
   Pattern: drone_channel, 5g_ch1, drone_channel, 5g_ch2, ... 
   Used for internal adapter to scan priority drone channel + selected 5G channels */
std::vector<int> generate_5ghz_interleaved(int drone_channel) {
    std::vector<int> result;
    
    /* Selected 5GHz channels for rapid scanning: 105, 131 */
    const int fiveghz_channels[] = {105, 131};
    
    if (drone_channel <= 0) {
        /* No drone channel, just use 5GHz channels */
        result.insert(result.end(), fiveghz_channels, fiveghz_channels + 2);
        return result;
    }
    
    /* Interleave drone channel with 5GHz channels: drone, 5g1, drone, 5g2, drone, 5g1... */
    result.push_back(drone_channel);
    for (size_t i = 0; i < 4; ++i) {  /* Multiple cycles to cover both 5GHz channels */
        for (const int ch : fiveghz_channels) {
            result.push_back(ch);
            result.push_back(drone_channel);
        }
    }
    return result;
}

int hop_interval_ms_from_env() {
    const char *raw = std::getenv("BDS_WIFI_RID_HOP_MS");
    if (!raw || !raw[0]) return 200;

    char *end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 60000) return 200;
    return (int)parsed;
}

bool adaptive_hop_enabled_from_env() {
    const char *raw = std::getenv("BDS_WIFI_RID_ADAPTIVE_HOP");
    if (!raw || !raw[0]) return true;
    if (std::strcmp(raw, "0") == 0) return false;
    if (strcasecmp(raw, "false") == 0) return false;
    if (strcasecmp(raw, "no") == 0) return false;
    if (strcasecmp(raw, "off") == 0) return false;
    return true;
}

void usage(const char *argv0) {
    std::fprintf(stderr,
                 "Usage: %s --iface IFACE [--udp-port PORT] [--obs-lat LAT --obs-lon LON] [--min-emit-ms MS] [--hop-ms MS]\n"
                 "          [--allow-ids CSV] [--block-ids CSV] [--debug-frames] [--any-vendor] [--dump-all-ies] [--self-check-only]\n"
                 "  --debug-frames   Print every Vendor IE (eid=221) with a full hex payload dump\n"
                 "  --any-vendor     Count any eid=221 Vendor IE as ODID OUI, bypassing the FA:0B:BC check\n"
                 "  --dump-all-ies   Print every IE with its eid and length from each management frame\n"
                 "  --self-check-only  Run startup validation only, then exit\n"
                 "Defaults: obs-lat=22.758423, obs-lon=120.337893, min-emit-ms=1, hop-ms=200\n"
                 "Env: BDS_WIFI_RID_CHANNELS=1,6,11, BDS_WIFI_RID_HOP_MS=200, BDS_WIFI_RID_BLOCK_IDS=mac_or_rid, BDS_WIFI_RID_ADAPTIVE_HOP=1\n",
                 argv0);
}

std::string lower_trimmed(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

void parse_id_filter_csv(const char *csv, std::vector<std::string> &out) {
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

bool parse_args(int argc, char **argv, Args &out) {
    out.hop_interval_ms = hop_interval_ms_from_env();
    if (const char *raw_hex_env = std::getenv("BDS_WIFI_RID_RAW_HEX")) {
        if (raw_hex_env[0] && std::strcmp(raw_hex_env, "0") != 0 &&
            strcasecmp(raw_hex_env, "false") != 0 &&
            strcasecmp(raw_hex_env, "no") != 0 &&
            strcasecmp(raw_hex_env, "off") != 0) {
            g_raw_hex_special_only = true;
        }
    }
    parse_id_filter_csv(std::getenv("BDS_WIFI_RID_BLOCK_IDS"), out.block_ids);
    if (out.block_ids.empty()) {
        parse_id_filter_csv(std::getenv("BDS_RID_BLOCK_IDS"), out.block_ids);
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
        } else if (std::strcmp(argv[i], "--hop-ms") == 0 && i + 1 < argc) {
            out.hop_interval_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--allow-ids") == 0 && i + 1 < argc) {
            parse_id_filter_csv(argv[++i], out.allow_ids);
        } else if (std::strcmp(argv[i], "--block-ids") == 0 && i + 1 < argc) {
            out.block_ids.clear();
            parse_id_filter_csv(argv[++i], out.block_ids);
        } else if (std::strcmp(argv[i], "--raw-hex") == 0) {
            g_raw_hex_special_only = true;
        } else if (std::strcmp(argv[i], "--debug-frames") == 0) {
            g_debug_frames = true;
        } else if (std::strcmp(argv[i], "--any-vendor") == 0) {
            g_any_vendor = true;
        } else if (std::strcmp(argv[i], "--dump-all-ies") == 0) {
            g_dump_all_ies = true;
        } else if (std::strcmp(argv[i], "--self-check-only") == 0) {
            out.self_check_only = true;
        } else if (std::strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            out.role = lower_trimmed(std::string(argv[++i]));
        } else if (std::strcmp(argv[i], "--channels") == 0 && i + 1 < argc) {
            out.channels_csv = argv[++i];
        } else {
            return false;
        }
    }
    if (out.iface.empty() || out.udp_port <= 0 || out.udp_port > 65535) return false;
    if (out.obs_lat < -90.0 || out.obs_lat > 90.0) return false;
    if (out.obs_lon < -180.0 || out.obs_lon > 180.0) return false;
    if (out.min_emit_ms < 1) out.min_emit_ms = 1;
    if (out.hop_interval_ms < 1) out.hop_interval_ms = 1;
    return true;
}

unsigned short rd16(const unsigned char *p) {
    return (unsigned short)p[0] | ((unsigned short)p[1] << 8);
}

bool extract_source_mac(const unsigned char *buf, size_t len, unsigned char out[6]) {
    if (len < 10) return false;

    const unsigned short rtap_len = rd16(buf + 2);
    if (rtap_len >= len || rtap_len < 8) return false;

    const unsigned char *d = buf + rtap_len;
    const size_t dlen = len - rtap_len;
    if (dlen < 24) return false;

    const unsigned short fc = rd16(d);
    const int type = (fc >> 2) & 0x3;
    const int tods = (fc & 0x0100) ? 1 : 0;
    const int fromds = (fc & 0x0200) ? 1 : 0;

    if (type == 0) {
        std::memcpy(out, d + 10, 6);
        return true;
    }

    if (type == 2) {
        if (!tods && !fromds) {
            std::memcpy(out, d + 10, 6);
            return true;
        }
        if (tods && !fromds) {
            std::memcpy(out, d + 10, 6);
            return true;
        }
        if (!tods && fromds) {
            std::memcpy(out, d + 16, 6);
            return true;
        }
        if (dlen >= 30) {
            std::memcpy(out, d + 24, 6);
            return true;
        }
    }

    return false;
}

bool extract_bssid(const unsigned char *buf, size_t len, unsigned char out[6]) {
    if (len < 10) return false;

    const unsigned short rtap_len = rd16(buf + 2);
    if (rtap_len >= len || rtap_len < 8) return false;

    const unsigned char *d = buf + rtap_len;
    const size_t dlen = len - rtap_len;
    if (dlen < 24) return false;

    const unsigned short fc = rd16(d);
    const int type = (fc >> 2) & 0x3;
    const int tods = (fc & 0x0100) ? 1 : 0;
    const int fromds = (fc & 0x0200) ? 1 : 0;

    if (type == 0) {
        std::memcpy(out, d + 16, 6);
        return true;
    }

    if (type == 2) {
        if (!tods && !fromds) {
            std::memcpy(out, d + 16, 6);
            return true;
        }
        if (tods && !fromds) {
            std::memcpy(out, d + 4, 6);
            return true;
        }
        if (!tods && fromds) {
            std::memcpy(out, d + 10, 6);
            return true;
        }
    }

    return false;
}

bool extract_radiotap_signal_dbm(const unsigned char *buf, size_t len, int &signal_dbm) {
    if (len < 8) return false;
    const unsigned short rtap_len = rd16(buf + 2);
    if (rtap_len > len || rtap_len < 8) return false;

    uint32_t present_words[8] = {0};
    size_t present_count = 0;
    size_t off = 4;
    bool ext = true;
    while (ext && off + 4 <= len && present_count < 8) {
        const uint32_t word = (uint32_t)buf[off] |
                              ((uint32_t)buf[off + 1] << 8) |
                              ((uint32_t)buf[off + 2] << 16) |
                              ((uint32_t)buf[off + 3] << 24);
        present_words[present_count++] = word;
        ext = (word & 0x80000000u) != 0;
        off += 4;
    }

    size_t data_off = off;
    for (size_t word_idx = 0; word_idx < present_count; ++word_idx) {
        const uint32_t present = present_words[word_idx];
        for (int bit = 0; bit < 32; ++bit) {
            if ((word_idx + 1 == present_count) && bit == 31) break;
            if (!(present & (1u << bit))) continue;

            const int field = (int)(word_idx * 32 + (size_t)bit);
            size_t align = 1;
            size_t size = 0;
            switch (field) {
                case 0: align = 8; size = 8; break;
                case 1: align = 1; size = 1; break;
                case 2: align = 1; size = 1; break;
                case 3: align = 2; size = 4; break;
                case 4: align = 1; size = 2; break;
                case 5: align = 1; size = 1; break;
                case 6: align = 1; size = 1; break;
                case 7: align = 2; size = 2; break;
                case 8: align = 2; size = 2; break;
                case 9: align = 2; size = 2; break;
                case 10: align = 1; size = 1; break;
                case 11: align = 1; size = 1; break;
                case 12: align = 1; size = 1; break;
                case 13: align = 1; size = 1; break;
                case 14: align = 2; size = 2; break;
                case 15: align = 2; size = 2; break;
                case 16: align = 1; size = 1; break;
                case 17: align = 1; size = 1; break;
                default: return false;
            }
            data_off = (data_off + align - 1) & ~(align - 1);
            if (data_off + size > (size_t)rtap_len) return false;
            if (field == 5) {
                signal_dbm = (int)((const int8_t *)buf)[data_off];
                return true;
            }
            data_off += size;
        }
    }
    return false;
}

bool extract_beacon_snapshot(const unsigned char *buf, size_t len,
                             unsigned char bssid[6], int &channel,
                             std::string &security, std::string &essid) {
    if (len < 10) return false;
    const unsigned short rtap_len = rd16(buf + 2);
    if (rtap_len >= len || rtap_len < 8) return false;

    const unsigned char *d = buf + rtap_len;
    const size_t dlen = len - rtap_len;
    if (dlen < 36) return false;

    const unsigned short fc = rd16(d);
    const int type = (fc >> 2) & 0x3;
    const int subtype = (fc >> 4) & 0x0f;
    if (type != 0 || (subtype != 8 && subtype != 5)) return false;

    std::memcpy(bssid, d + 16, 6);
    const unsigned short capab = rd16(d + 24 + 10);
    const bool privacy = (capab & 0x0010u) != 0;

    bool has_rsn = false;
    bool has_wpa = false;
    channel = 0;
    essid.clear();

    const unsigned char *ies = d + 24 + 12;
    size_t ies_len = dlen - (24 + 12);
    size_t p = 0;
    while (p + 2 <= ies_len) {
        const unsigned char eid = ies[p];
        const unsigned char elen = ies[p + 1];
        if (p + 2 + elen > ies_len) break;
        const unsigned char *edata = ies + p + 2;

        if (eid == 0) {
            if (elen == 0) {
                essid = "<hidden>";
            } else {
                essid.assign((const char *)edata, (const char *)edata + elen);
            }
        } else if (eid == 3 && elen >= 1) {
            channel = (int)edata[0];
        } else if (eid == 48) {
            has_rsn = true;
        } else if (eid == 221 && elen >= 4 &&
                   edata[0] == 0x00 && edata[1] == 0x50 && edata[2] == 0xf2 && edata[3] == 0x01) {
            has_wpa = true;
        }

        p += (size_t)elen + 2;
    }

    if (essid.empty()) essid = "<hidden>";
    if (!privacy) {
        security = "OPN";
    } else if (has_rsn) {
        security = "WPA2";
    } else if (has_wpa) {
        security = "WPA";
    } else {
        security = "WEP";
    }

    return true;
}

/* MAC address formatting moved to platform::mac_to_string */
/* Observer file parsing moved to platform::parse_observer_position */

/* Radiotap and beacon extraction helpers follow */

/* Geometry functions moved to geometry.hpp namespace (geo::*) */

bool decode_odid_location_msg(const unsigned char *msg, size_t len,
                              double &lat_deg, double &lon_deg) {
    if (len < 25) return false;
    if (((msg[0] >> 4) & 0x0f) != 1) return false;

    const int32_t lat_raw = odid::le_i32(msg + 5);
    const int32_t lon_raw = odid::le_i32(msg + 9);
    const double lat = (double)lat_raw * 1e-7;
    const double lon = (double)lon_raw * 1e-7;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
    if (std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9) return false;

    lat_deg = lat;
    lon_deg = lon;
    return true;
}

bool decode_odid_location_from_blob(const unsigned char *blob, size_t len,
                                    double &lat_deg, double &lon_deg) {
    if (len < 25) return false;
    for (size_t i = 0; i + 25 <= len; ++i) {
        if (decode_odid_location_msg(blob + i, len - i, lat_deg, lon_deg)) {
            return true;
        }
    }
    return false;
}

bool decode_odid_system_msg(const unsigned char *msg, size_t len,
                            double &op_lat_deg, double &op_lon_deg,
                            int16_t &altitude_m) {
    if (len < 25) return false;
    if (((msg[0] >> 4) & 0x0f) != 4) return false;

    const int32_t op_lat_raw = odid::le_i32(msg + 5);
    const int32_t op_lon_raw = odid::le_i32(msg + 9);
    const double op_lat = (double)op_lat_raw * 1e-7;
    const double op_lon = (double)op_lon_raw * 1e-7;
    if (op_lat < -90.0 || op_lat > 90.0 || op_lon < -180.0 || op_lon > 180.0) return false;
    if (std::abs(op_lat) < 1e-9 && std::abs(op_lon) < 1e-9) return false;

    const int16_t alt_raw = (int16_t)(msg[13] | ((int16_t)msg[14] << 8));
    altitude_m = alt_raw;

    op_lat_deg = op_lat;
    op_lon_deg = op_lon;
    return true;
}

bool decode_odid_system_from_blob(const unsigned char *blob, size_t len,
                                  double &op_lat_deg, double &op_lon_deg,
                                  int16_t &altitude_m) {
    if (len < 25) return false;
    for (size_t i = 0; i + 25 <= len; ++i) {
        if (decode_odid_system_msg(blob + i, len - i, op_lat_deg, op_lon_deg, altitude_m)) {
            return true;
        }
    }
    return false;
}

bool decode_odid_vector_msg(const unsigned char *msg, size_t len,
                            int16_t &ground_speed_cms, double &track_deg,
                            int16_t &vertical_speed_cms) {
    if (len < 25) return false;
    if (((msg[0] >> 4) & 0x0f) != 2) return false;

    const int16_t gs_raw = (int16_t)(msg[2] | ((int16_t)msg[3] << 8));
    const int16_t track_raw = (int16_t)(msg[4] | ((int16_t)msg[5] << 8));
    const int16_t vs_raw = (int16_t)(msg[6] | ((int16_t)msg[7] << 8));

    ground_speed_cms = gs_raw;
    track_deg = (double)track_raw * 0.01;
    vertical_speed_cms = vs_raw;
    return true;
}

bool decode_odid_vector_from_blob(const unsigned char *blob, size_t len,
                                  int16_t &ground_speed_cms, double &track_deg,
                                  int16_t &vertical_speed_cms) {
    if (len < 25) return false;
    for (size_t i = 0; i + 25 <= len; ++i) {
        if (decode_odid_vector_msg(blob + i, len - i, ground_speed_cms, track_deg, vertical_speed_cms)) {
            return true;
        }
    }
    return false;
}

std::string decode_ascii_id_field(const unsigned char *p, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = p[i];
        if (c == 0) break;
        if (c >= 32 && c <= 126) {
            if (c == '"' || c == '\\') {
                out.push_back('_');
            } else {
                out.push_back((char)c);
            }
        } else {
            break;
        }
    }
    while (!out.empty() && std::isspace((unsigned char)out.back())) out.pop_back();
    return out;
}

bool decode_odid_basic_id_from_blob(const unsigned char *blob, size_t len,
                                    std::string &out_id) {
    if (len < 25) return false;
    std::string best;
    for (size_t i = 0; i + 25 <= len; ++i) {
        const unsigned char *msg = blob + i;
        if (((msg[0] >> 4) & 0x0f) != 0) continue;

        const std::string c1 = decode_ascii_id_field(msg + 2, 20);
        const std::string c2 = decode_ascii_id_field(msg + 5, 20);
        const std::string &cand = (c2.size() > c1.size()) ? c2 : c1;
        if (cand.size() < 3) continue;
        if (cand.size() > best.size()) best = cand;
    }
    if (best.empty()) return false;
    out_id = best;
    return true;
}

bool id_allowed(const std::vector<std::string> &allow_ids,
                const std::string &device_id,
                const std::string &remote_id) {
    if (allow_ids.empty()) return true;
    const std::string did = lower_trimmed(device_id);
    const std::string rid = lower_trimmed(remote_id);
    for (const std::string &w : allow_ids) {
        if ((!did.empty() && did == w) || (!rid.empty() && rid == w)) {
            return true;
        }
    }
    return false;
}

bool id_blocked(const std::vector<std::string> &block_ids,
                const std::string &device_id,
                const std::string &remote_id) {
    if (block_ids.empty()) return false;
    const std::string did = lower_trimmed(device_id);
    const std::string rid = lower_trimmed(remote_id);
    for (const std::string &b : block_ids) {
        if ((!did.empty() && did == b) || (!rid.empty() && rid == b)) {
            return true;
        }
    }
    return false;
}

std::string emit_bridge_status_json(const StatsWindow &st,
                                    const bridge_check::RuntimeHealth &health,
                                    const std::string &iface,
                                    int current_channel) {
    const double hit_rate = (st.odid > 0) ? (100.0 * (double)st.loc / (double)st.odid) : 0.0;
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

    char out[1200];
    std::snprintf(out, sizeof(out),
                  "{"
                  "\"bridge_status\":true,"
                  "\"detected\":false,"
                  "\"confidence\":0.0,"
                  "\"source\":\"wifi-rid-status\","
                  "\"bridge\":\"wifi-rid-bridge\","
                  "\"protocol\":\"Wi-Fi\","
                  "\"iface\":\"%s\","
                  "\"role\":\"%s\","
                  "\"channel\":%d,"
                  "\"rx\":%llu,"
                  "\"oui\":%llu,"
                  "\"odid\":%llu,"
                  "\"loc\":%llu,"
                  "\"dropped\":%llu,"
                  "\"searcher_hit\":%llu,"
                  "\"tracker_followup_hit\":%llu,"
                  "\"tracker_lock_event\":%llu,"
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
                  iface.c_str(),
                  (g_bridge_role == ROLE_SEARCHER) ? "searcher"
                  : (g_bridge_role == ROLE_TRACKER) ? "tracker" : "standard",
                  current_channel,
                  (unsigned long long)st.rx,
                  (unsigned long long)st.oui,
                  (unsigned long long)st.odid,
                  (unsigned long long)st.loc,
                  (unsigned long long)st.dropped,
                  (unsigned long long)st.searcher_hit,
                  (unsigned long long)st.tracker_followup_hit,
                  (unsigned long long)st.tracker_lock_event,
                  hit_rate,
                  health.recovery_count,
                  health.idle_windows,
                  health.no_signature_windows,
                  health.last_recovery_ms,
                  health_status,
                  health_detail);
    return std::string(out);
}

void flush_stats_if_due(StatsWindow &st,
                        bridge_check::RuntimeHealth &health,
                        int udp,
                        const struct sockaddr_in &dst,
                        const std::string &iface,
                        long long now,
                        int current_channel) {
    if (st.window_start_ms == 0) {
        st.window_start_ms = now;
        return;
    }
    if (now - st.window_start_ms < 1000) return;

    const double hit_rate = (st.odid > 0) ? (100.0 * (double)st.loc / (double)st.odid) : 0.0;
    std::fprintf(stderr,
                 "[wifi-rid-bridge] 1s stats | Rx: %llu | OUI: %llu | ODID: %llu | Loc: %llu | Dropped: %llu | SearcherHit: %llu | TrackerFollowUp: %llu | TrackerLock: %llu -> (Hit Rate: %.1f%%)\n",
                 (unsigned long long)st.rx,
                 (unsigned long long)st.oui,
                 (unsigned long long)st.odid,
                 (unsigned long long)st.loc,
                 (unsigned long long)st.dropped,
                 (unsigned long long)st.searcher_hit,
                 (unsigned long long)st.tracker_followup_hit,
                 (unsigned long long)st.tracker_lock_event,
                 hit_rate);

    bridge_check::report_runtime_health("wifi-rid-bridge", health, st.rx, st.oui);

    const std::string status_json = emit_bridge_status_json(st, health, iface, current_channel);
    const ssize_t status_sent = sendto(udp, status_json.data(), status_json.size(), MSG_NOSIGNAL,
                                       (const struct sockaddr *)&dst, sizeof(dst));
    if (status_sent < 0) {
        std::fprintf(stderr, "[wifi-rid-bridge] status sendto failed: %s\n", std::strerror(errno));
    }

    st.rx = 0;
    st.oui = 0;
    st.odid = 0;
    st.loc = 0;
    st.dropped = 0;
    st.searcher_hit = 0;
    st.tracker_followup_hit = 0;
    st.tracker_lock_event = 0;
    st.window_start_ms = now;
}

void hop_channel_if_due(const std::string &iface,
                       int hop_interval_ms,
                       long long now_ms_value,
                       long long &last_hop_ms,
                       size_t &hop_index,
                       int &current_channel,
                       bool adaptive_enabled,
                       int &locked_channel,
                       bool &locked_hit_since_hop) {
    if (iface.empty()) return;
    if (g_hop_channels.empty()) return;
    if (now_ms_value - last_hop_ms < hop_interval_ms) return;

    if (adaptive_enabled && locked_channel > 0) {
        if (!locked_hit_since_hop) {
            std::fprintf(stderr,
                         "[wifi-rid-bridge] adaptive-hop miss on locked ch %d; fallback to full scan\n",
                         locked_channel);
            locked_channel = -1;
        }
        locked_hit_since_hop = false;
    }

    last_hop_ms = now_ms_value;
    int channel = -1;
    
    /* Select channel sequence based on adapter type and mode */
    std::vector<int> active_channels;
    
    if (g_fiveghz_interleave) {
        /* Internal adapter: fixed drone channel once detected, else 5GHz scanning */
        if (g_last_drone_channel > 0) {
            /* Drone detected: stay on that channel only */
            active_channels = generate_fixed_drone_channel(g_last_drone_channel);
        } else {
            /* No drone yet: scan 5GHz channels */
            active_channels = generate_5ghz_interleaved(-1);
        }
    } else if (g_weighted_hop_external) {
        /* External adapter: weighted hopping with drone channel priority */
        active_channels = generate_weighted_channels(g_last_drone_channel);
    } else {
        /* Standard hopping */
        active_channels = g_hop_channels;
    }
    
    if (adaptive_enabled && locked_channel > 0) {
        channel = locked_channel;
    } else if (!active_channels.empty()) {
        channel = active_channels[hop_index % active_channels.size()];
        hop_index = (hop_index + 1) % active_channels.size();
    } else {
        channel = g_hop_channels[hop_index % g_hop_channels.size()];
        hop_index = (hop_index + 1) % g_hop_channels.size();
    }

    char cmd[256];
    std::snprintf(cmd, sizeof(cmd),
                  "iw dev %s set channel %d >/dev/null 2>&1",
                  iface.c_str(), channel);
    const int rc = std::system(cmd);
    (void)rc;
    current_channel = channel;
}

bool extract_drone_llh(const unsigned char *buf, size_t len,
                       double &lat_deg, double &lon_deg,
                       bool *out_odid_signature,
                       std::string *out_remote_id_id,
                       std::string *out_raw_hex,
                       double *out_operator_lat,
                       double *out_operator_lon,
                       int16_t *out_operator_alt,
                       int16_t *out_drone_alt,
                       double *out_track_deg,
                       int16_t *out_ground_speed_cms) {
    if (out_odid_signature) *out_odid_signature = false;
    if (out_remote_id_id) out_remote_id_id->clear();
    if (out_raw_hex) out_raw_hex->clear();
    if (out_operator_lat) *out_operator_lat = 0.0;
    if (out_operator_lon) *out_operator_lon = 0.0;
    if (out_operator_alt) *out_operator_alt = 0;
    if (out_drone_alt) *out_drone_alt = 0;
    if (out_track_deg) *out_track_deg = 0.0;
    if (out_ground_speed_cms) *out_ground_speed_cms = 0;
    
    if (len < 10) return false;
    const unsigned short rtap_len = rd16(buf + 2);
    if (rtap_len >= len || rtap_len < 8) return false;

    const unsigned char *d = buf + rtap_len;
    const size_t dlen = len - rtap_len;
    if (dlen < 24) return false;

    const unsigned short fc = rd16(d);
    const int type = (fc >> 2) & 0x3;
    if (type != 0) return false;

    const int subtype = (fc >> 4) & 0x0f;
    size_t body_off = 24;
    if (subtype == 8 || subtype == 5) {
        body_off += 12;
    }
    if (body_off >= dlen) return false;

    const unsigned char *ies = d + body_off;
    size_t ies_len = dlen - body_off;

    bool drone_loc_valid = false;
    size_t p = 0;
    while (p + 2 <= ies_len) {
        const unsigned char eid = ies[p];
        const unsigned char elen = ies[p + 1];
        if (p + 2 + elen > ies_len) break;

        const unsigned char *edata = ies + p + 2;
        if (g_dump_all_ies) {
            std::fprintf(stderr,
                "[wifi-rid-bridge] IE eid=%3d len=%3d\n", (int)eid, (int)elen);
        }

        if (eid == 221 && elen >= 4) {
            if (g_debug_frames) {
                /* Print OUI + full hex payload for Parrot/non-standard identification */
                std::fprintf(stderr,
                    "[wifi-rid-bridge] Vendor IE OUI:%02x:%02x:%02x len=%d payload:",
                    edata[0], edata[1], edata[2], (int)elen);
                const size_t dump_len = ((size_t)elen < 40u) ? (size_t)elen : 40u;
                for (size_t di = 3; di < dump_len; ++di)
                    std::fprintf(stderr, " %02x", edata[di]);
                std::fprintf(stderr, "%s\n", ((size_t)elen > 40u) ? " ..." : "");
            }
            const bool is_odid_oui = (edata[0] == 0xfa && edata[1] == 0x0b && edata[2] == 0xbc)
                                   || g_any_vendor;
            if (is_odid_oui) {
                if (out_odid_signature) *out_odid_signature = true;
                if (out_remote_id_id) {
                    std::string rid;
                    if (decode_odid_basic_id_from_blob(edata + 3, (size_t)elen - 3, rid)) {
                        *out_remote_id_id = rid;
                    }
                }
                if (!drone_loc_valid && decode_odid_location_from_blob(edata + 3, (size_t)elen - 3, lat_deg, lon_deg)) {
                    drone_loc_valid = true;
                    if (out_raw_hex) {
                        *out_raw_hex = bytes_to_hex_string(edata, (size_t)elen);
                    }
                    if (out_drone_alt) {
                        int16_t alt = 0;
                        double dummy_op_lat, dummy_op_lon;
                        if (decode_odid_system_from_blob(edata + 3, (size_t)elen - 3, dummy_op_lat, dummy_op_lon, alt)) {
                            *out_drone_alt = alt;
                        }
                    }
                }
                if (out_operator_lat && out_operator_lon) {
                    int16_t op_alt = 0;
                    if (decode_odid_system_from_blob(edata + 3, (size_t)elen - 3, *out_operator_lat, *out_operator_lon, op_alt)) {
                        if (out_operator_alt) *out_operator_alt = op_alt;
                    }
                }
                if (out_track_deg && out_ground_speed_cms) {
                    int16_t dummy_vs = 0;
                    decode_odid_vector_from_blob(edata + 3, (size_t)elen - 3, *out_ground_speed_cms, *out_track_deg, dummy_vs);
                }
            }
        }
        p += (size_t)elen + 2;
    }

    return drone_loc_valid;
}

std::string emit_json(const unsigned char mac[6],
                      double obs_lat, double obs_lon,
                      double drone_lat, double drone_lon,
                      const std::string &remote_id_id,
                      double operator_lat = 0.0,
                      double operator_lon = 0.0,
                      bool has_operator = false) {
    const double distance = geo::haversine_m(obs_lat, obs_lon, drone_lat, drone_lon);
    const double bearing = geo::bearing_deg(obs_lat, obs_lon, drone_lat, drone_lon);
    const double confidence = 0.93;

    char out[1536];
    bool opok = has_operator && (std::abs(operator_lat) > odid::COORDINATE_TOLERANCE || std::abs(operator_lon) > odid::COORDINATE_TOLERANCE);
    
    if (opok) {
        const double op_distance = geo::haversine_m(obs_lat, obs_lon, operator_lat, operator_lon);
        const double op_bearing = geo::bearing_deg(obs_lat, obs_lon, operator_lat, operator_lon);
        if (remote_id_id.empty()) {
            std::snprintf(out, sizeof(out),
                      "{\"detected\":true,\"confidence\":%.2f,\"source\":\"wifi-rid\","
                      "\"vendor\":\"odid\",\"model\":\"wifi\","
                      "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                      "\"device_id\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                      "\"oui\":\"%02x:%02x:%02x\",\"remote_id\":true,\"rid\":true,"
                      "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                      "\"operator_lat\":%.7f,\"operator_lon\":%.7f,"
                      "\"bearing_deg\":%.1f,\"distance_m\":%.1f,"
                      "\"operator_bearing_deg\":%.1f,\"operator_distance_m\":%.1f,"
                      "\"has_bearing\":true,\"has_distance\":true,\"has_operator\":true}",
                      confidence,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      mac[0], mac[1], mac[2],
                      drone_lat, drone_lon,
                      operator_lat, operator_lon,
                      bearing, distance,
                      op_bearing, op_distance);
        } else {
            std::snprintf(out, sizeof(out),
                      "{\"detected\":true,\"confidence\":%.2f,\"source\":\"wifi-rid\","
                      "\"vendor\":\"odid\",\"model\":\"wifi\","
                      "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                      "\"device_id\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                      "\"remote_id_id\":\"%s\",\"oui\":\"%02x:%02x:%02x\",\"remote_id\":true,\"rid\":true,"
                      "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                      "\"operator_lat\":%.7f,\"operator_lon\":%.7f,"
                      "\"bearing_deg\":%.1f,\"distance_m\":%.1f,"
                      "\"operator_bearing_deg\":%.1f,\"operator_distance_m\":%.1f,"
                      "\"has_bearing\":true,\"has_distance\":true,\"has_operator\":true}",
                      confidence,
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      remote_id_id.c_str(),
                      mac[0], mac[1], mac[2],
                      drone_lat, drone_lon,
                      operator_lat, operator_lon,
                      bearing, distance,
                      op_bearing, op_distance);
        }
    } else if (remote_id_id.empty()) {
        std::snprintf(out, sizeof(out),
                  "{\"detected\":true,\"confidence\":%.2f,\"source\":\"wifi-rid\","
                  "\"vendor\":\"odid\",\"model\":\"wifi\","
                  "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"device_id\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"oui\":\"%02x:%02x:%02x\",\"remote_id\":true,\"rid\":true,"
                  "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                  "\"bearing_deg\":%.1f,\"distance_m\":%.1f,"
                  "\"has_bearing\":true,\"has_distance\":true,\"has_operator\":false}",
                  confidence,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  mac[0], mac[1], mac[2],
                  drone_lat, drone_lon,
                  bearing, distance);
    } else {
        std::snprintf(out, sizeof(out),
                  "{\"detected\":true,\"confidence\":%.2f,\"source\":\"wifi-rid\","
                  "\"vendor\":\"odid\",\"model\":\"wifi\","
                  "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"device_id\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"remote_id_id\":\"%s\",\"oui\":\"%02x:%02x:%02x\",\"remote_id\":true,\"rid\":true,"
                  "\"drone_lat\":%.7f,\"drone_lon\":%.7f,"
                  "\"bearing_deg\":%.1f,\"distance_m\":%.1f,"
                  "\"has_bearing\":true,\"has_distance\":true,\"has_operator\":false}",
                  confidence,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  remote_id_id.c_str(),
                  mac[0], mac[1], mac[2],
                  drone_lat, drone_lon,
                  bearing, distance);
    }
    return std::string(out);
}

std::string emit_monitor_json(const unsigned char bssid[6],
                              int signal_dbm,
                              int channel,
                              const std::string &security,
                              const std::string &essid,
                              uint64_t beacon_count,
                              uint64_t data_count) {
    char safe_essid[128];
    size_t j = 0;
    for (size_t i = 0; i < essid.size() && j + 1 < sizeof(safe_essid); ++i) {
        const unsigned char c = (unsigned char)essid[i];
        if (c == '"' || c == '\\') {
            safe_essid[j++] = '_';
        } else if (c >= 32 && c <= 126) {
            safe_essid[j++] = (char)c;
        }
    }
    safe_essid[j] = '\0';

    char out[768];
    std::snprintf(out, sizeof(out),
                  "{"
                  "\"detected\":false,"
                  "\"confidence\":0.10,"
                  "\"source\":\"wifi-beacon\","
                  "\"vendor\":\"%s\","
                  "\"model\":\"%s\","
                  "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"device_id\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                  "\"oui\":\"%02x:%02x:%02x\","
                  "\"rssi_dbm\":%d,"
                  "\"channel\":%d,"
                  "\"security\":\"%s\","
                  "\"essid\":\"%s\","
                  "\"beacon_count\":%llu,"
                  "\"data_count\":%llu,"
                  "\"remote_id\":false"
                  "}",
                  security.c_str(), safe_essid,
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                  bssid[0], bssid[1], bssid[2],
                  signal_dbm,
                  channel,
                  security.c_str(), safe_essid,
                  (unsigned long long)beacon_count,
                  (unsigned long long)data_count);
    return std::string(out);
}

}  // namespace

int main(int argc, char **argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        usage(argv[0]);
        return 2;
    }

    init_hop_channels_from_env();

    /* Apply per-instance channel override from --channels CLI arg */
    if (!args.channels_csv.empty()) {
        g_hop_channels.clear();
        parse_channel_list_csv(args.channels_csv.c_str(), g_hop_channels);
    }

    /* Configure hopping mode based on explicit role or interface name */
    if (args.role == "searcher") {
        g_bridge_role = ROLE_SEARCHER;
        g_weighted_hop_external = true;
        g_fiveghz_interleave = false;
        std::fprintf(stderr, "[wifi-rid-bridge] role: searcher (notifies tracker on drone detect)\n");
    } else if (args.role == "tracker") {
        g_bridge_role = ROLE_TRACKER;
        g_weighted_hop_external = false;
        g_fiveghz_interleave = false;
        /* Park on ch1 as placeholder; IPC overrides when searcher finds drone */
        g_hop_channels.assign(1, 1);
        std::fprintf(stderr, "[wifi-rid-bridge] role: tracker (parked ch1, awaiting IPC signal)\n");
    } else {
        /* Auto-detect from interface name */
        if (args.iface.find("wlx") == 0) {
            g_weighted_hop_external = true;
            std::fprintf(stderr, "[wifi-rid-bridge] external adapter detected: weighted hopping enabled\n");
        } else if (args.iface.find("wlp") == 0 || args.iface.find("mon") != std::string::npos) {
            g_fiveghz_interleave = true;
            std::fprintf(stderr, "[wifi-rid-bridge] internal adapter detected: 5GHz interleaving enabled\n");
        }
    }

    std::vector<bridge_check::CheckResult> checks;
    checks.push_back(bridge_check::check_udp_port(args.udp_port));
    checks.push_back(bridge_check::check_coordinates(args.obs_lat, args.obs_lon));
    checks.push_back(bridge_check::check_observer_cache_file());
    checks.push_back(bridge_check::check_raw_capture_permissions("raw-capture-permissions"));
    checks.push_back(bridge_check::check_interface_exists(args.iface));
    checks.push_back(bridge_check::check_channel_hop_count(g_hop_channels.size()));
    if (!bridge_check::print_report("wifi-rid-bridge", checks)) {
        return 2;
    }
    if (args.self_check_only) {
        std::fprintf(stderr, "[wifi-rid-bridge] self-check only mode: validation complete, exiting before capture\n");
        return 0;
    }

    const int ifindex = if_nametoindex(args.iface.c_str());
    if (ifindex == 0) {
        std::fprintf(stderr, "[wifi-rid-bridge] network interface not found: %s\n", args.iface.c_str());
        return 2;
    }

    int rx = open_packet_socket_with_recovery(args.iface, "startup");
    if (rx < 0) {
        std::fprintf(stderr,
                     "[wifi-rid-bridge] unable to open packet capture on %s after auto-repair retries\n",
                     args.iface.c_str());
        return 2;
    }

    /* Tracker: set short recv timeout so IPC polling is responsive even on quiet channels */
    if (g_bridge_role == ROLE_TRACKER) {
        const int tracker_recv_timeout_ms =
            env_int_clamped("BDS_WIFI_RID_TRACKER_RECV_TIMEOUT_MS", 50, 5, 500);
        struct timeval tv;
        tv.tv_sec = tracker_recv_timeout_ms / 1000;
        tv.tv_usec = (tracker_recv_timeout_ms % 1000) * 1000;
        setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    int udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp < 0) {
        std::fprintf(stderr, "[wifi-rid-bridge] UDP socket failed: %s\n", std::strerror(errno));
        close(rx);
        return 2;
    }

    struct sockaddr_in dst {};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((unsigned short)args.udp_port);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    std::unordered_map<std::string, SeenEntry> seen;
    std::unordered_map<std::string, MonitorApEntry> monitor_aps;
    std::vector<unsigned char> buf(65536);
    StatsWindow stats;
    bridge_check::RuntimeHealth health;

    double obs_lat = args.obs_lat;
    double obs_lon = args.obs_lon;
    long long last_obs_refresh_ms = 0;
    long long last_hop_ms = 0;
    size_t hop_index = 0;
    int current_channel = -1;
    const bool adaptive_hop = adaptive_hop_enabled_from_env() && g_hop_channels.size() > 1;
    int locked_channel = -1;
    bool locked_hit_since_hop = false;
    long long last_tracker_ipc_ms = 0;
    const int tracker_poll_ms = env_int_clamped("BDS_WIFI_RID_TRACKER_POLL_MS", 25, 5, 1000);
    const int tracker_stale_ms = env_int_clamped("BDS_WIFI_RID_TRACKER_STALE_MS", 3000, 200, 30000);
    const int ipc_min_rssi_dbm = env_int_clamped("BDS_WIFI_RID_IPC_MIN_RSSI", -82, -120, -20);
    const int ipc_switch_delta_db = env_int_clamped("BDS_WIFI_RID_IPC_SWITCH_DELTA_DB", 6, 0, 30);

    std::fprintf(stderr,
                 "[wifi-rid-bridge] startup: %s -> 127.0.0.1:%d, observer=(%.6f,%.6f)\n",
                 args.iface.c_str(), args.udp_port, obs_lat, obs_lon);
    std::fprintf(stderr,
                 "[wifi-rid-bridge] channel-hop list: %s (set BDS_WIFI_RID_BAND=5g or BDS_WIFI_RID_CHANNELS=...)\n",
                 hop_channels_to_string().c_str());
    std::fprintf(stderr,
                 "[wifi-rid-bridge] channel-hop interval: %d ms per channel\n",
                 args.hop_interval_ms);
    std::fprintf(stderr,
                 "[wifi-rid-bridge] adaptive-hop: %s\n",
                 adaptive_hop ? "enabled (lock hit channel, miss->full scan)" : "disabled");
    if (!args.allow_ids.empty()) {
        std::fprintf(stderr, "[wifi-rid-bridge] allow-ids filter enabled (%zu items)\n",
                     args.allow_ids.size());
    }
    if (!args.block_ids.empty()) {
        std::fprintf(stderr, "[wifi-rid-bridge] block-ids filter enabled (%zu items)\n",
                     args.block_ids.size());
    }
    if (g_bridge_role == ROLE_TRACKER) {
        std::fprintf(stderr,
                     "[wifi-rid-bridge] tracker-timing: poll=%dms stale=%dms\n",
                     tracker_poll_ms, tracker_stale_ms);
    }
    if (g_bridge_role == ROLE_SEARCHER || g_bridge_role == ROLE_STANDARD) {
        std::fprintf(stderr,
                     "[wifi-rid-bridge] ipc-arb: min-rssi=%d dBm switch-delta=%d dB\n",
                     ipc_min_rssi_dbm, ipc_switch_delta_db);
    }

    while (true) {
        const long long loop_now_ms = platform::now_ms();

        /* Tracker role: poll IPC signal at high rate to reduce phase-lag against beacon cadence. */
        if (g_bridge_role == ROLE_TRACKER && loop_now_ms - last_tracker_ipc_ms >= tracker_poll_ms) {
            last_tracker_ipc_ms = loop_now_ms;
            const TrackerChannelRecord rec = read_tracker_channel_record(loop_now_ms, tracker_stale_ms);
            const int tracked_ch = rec.valid ? rec.channel : -1;
            if (tracked_ch != g_last_drone_channel) {
                g_last_drone_channel = tracked_ch;
                g_hop_channels.clear();
                if (tracked_ch > 0) {
                    g_hop_channels.push_back(tracked_ch);
                    stats.tracker_lock_event++;
                    g_tracker_followup_pending = true;
                    g_tracker_followup_deadline_ms = loop_now_ms + 3000;
                    if (rec.rssi_dbm != -127) {
                        std::fprintf(stderr,
                                     "[wifi-rid-bridge] tracker: locked to channel %d (src=%s rssi=%d dBm)\n",
                                     tracked_ch,
                                     rec.iface.empty() ? "unknown" : rec.iface.c_str(),
                                     rec.rssi_dbm);
                    } else {
                        std::fprintf(stderr,
                                     "[wifi-rid-bridge] tracker: locked to channel %d (src=%s)\n",
                                     tracked_ch,
                                     rec.iface.empty() ? "unknown" : rec.iface.c_str());
                    }
                } else {
                    g_hop_channels.assign(1, 1);  /* Return to ch1 placeholder */
                    g_tracker_followup_pending = false;
                    g_tracker_followup_deadline_ms = 0;
                    std::fprintf(stderr, "[wifi-rid-bridge] tracker: signal lost, parked ch1\n");
                }
            }
        }

        flush_stats_if_due(stats, health, udp, dst, args.iface, loop_now_ms, current_channel);
        hop_channel_if_due(args.iface,
                   args.hop_interval_ms,
                   loop_now_ms,
                   last_hop_ms,
                   hop_index,
                   current_channel,
                   adaptive_hop,
                   locked_channel,
                   locked_hit_since_hop);

        const ssize_t n = recv(rx, buf.data(), buf.size(), 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == ENETDOWN || errno == ENODEV || errno == EIO) {
                std::fprintf(stderr,
                             "[wifi-rid-bridge] recv hit recoverable error (%s); attempting auto-repair\n",
                             std::strerror(errno));
                close(rx);
                rx = open_packet_socket_with_recovery(args.iface, "recv recovery");
                if (rx >= 0) {
                    bridge_check::note_recovery(health, loop_now_ms);
                    continue;
                }
                std::fprintf(stderr,
                             "[wifi-rid-bridge] auto-repair failed after recv error on %s\n",
                             args.iface.c_str());
            }
            std::fprintf(stderr, "[wifi-rid-bridge] recv failed: %s\n", std::strerror(errno));
            break;
        }
        stats.rx++;
        if (n < 24) continue;

        unsigned char mac[6] = {0};
        if (!extract_source_mac(buf.data(), (size_t)n, mac)) continue;

        const long long tnow = platform::now_ms();
        if (tnow - last_obs_refresh_ms > 1000) {
            last_obs_refresh_ms = tnow;
            (void)platform::parse_observer_position(obs_lat, obs_lon);
        }

        const std::string macs = platform::mac_to_string(mac);
        int frame_signal_dbm = -127;
        const bool has_frame_signal = extract_radiotap_signal_dbm(buf.data(), (size_t)n, frame_signal_dbm);
        if (!has_frame_signal) frame_signal_dbm = -127;

        unsigned char bssid[6] = {0};
        if (extract_bssid(buf.data(), (size_t)n, bssid)) {
            const std::string bssids = platform::mac_to_string(bssid);
            MonitorApEntry &ap = monitor_aps[bssids];

            const unsigned short rtap_len = rd16(buf.data() + 2);
            const unsigned char *d = buf.data() + rtap_len;
            const unsigned short fc = rd16(d);
            const int type = (fc >> 2) & 0x3;
            const int subtype = (fc >> 4) & 0x0f;

            int signal_dbm = 0;
            if (extract_radiotap_signal_dbm(buf.data(), (size_t)n, signal_dbm)) {
                ap.signal_dbm = signal_dbm;
            }

            if (type == 0 && (subtype == 8 || subtype == 5)) {
                int channel = ap.channel;
                std::string security = ap.security;
                std::string essid = ap.essid;
                unsigned char beacon_bssid[6] = {0};
                if (extract_beacon_snapshot(buf.data(), (size_t)n, beacon_bssid, channel, security, essid)) {
                    ap.channel = channel;
                    ap.security = security;
                    ap.essid = essid;
                    ap.beacon_count++;
                }
            } else if (type == 2) {
                ap.data_count++;
            }

            if ((ap.beacon_count > 0 || ap.data_count > 0) && (tnow - ap.last_emit_ms >= 1500)) {
                ap.last_emit_ms = tnow;
                const std::string mon_json = emit_monitor_json(bssid,
                                                               ap.signal_dbm,
                                                               ap.channel,
                                                               ap.security.empty() ? std::string("UNK") : ap.security,
                                                               ap.essid.empty() ? std::string("<hidden>") : ap.essid,
                                                               ap.beacon_count,
                                                               ap.data_count);
                const ssize_t mon_sent = sendto(udp, mon_json.data(), mon_json.size(), MSG_NOSIGNAL,
                                                (struct sockaddr *)&dst, sizeof(dst));
                if (mon_sent < 0) {
                    std::fprintf(stderr, "[wifi-rid-bridge] monitor sendto failed: %s\n", std::strerror(errno));
                }
            }
        }

        double drone_lat = 0.0;
        double drone_lon = 0.0;
        double operator_lat = 0.0;
        double operator_lon = 0.0;
        int16_t operator_alt = 0;
        int16_t drone_alt = 0;
        double track_deg = 0.0;
        int16_t ground_speed_cms = 0;
        std::string remote_id_id;
        std::string raw_hex_payload;
        bool odid_signature = false;
        if (!extract_drone_llh(buf.data(), (size_t)n, drone_lat, drone_lon,
                               &odid_signature, &remote_id_id,
                       &raw_hex_payload,
                               &operator_lat, &operator_lon,
                               &operator_alt, &drone_alt,
                               &track_deg, &ground_speed_cms)) {
            if (odid_signature) {
                stats.oui++;
                stats.odid++;
                stats.dropped++;
            }
            continue;
        }
        if (odid_signature) {
            stats.oui++;
            stats.odid++;
        }

        if (id_blocked(args.block_ids, macs, remote_id_id)) {
            continue;
        }
        if (!id_allowed(args.allow_ids, macs, remote_id_id)) {
            continue;
        }
        if (g_raw_hex_special_only && !raw_hex_payload.empty() && is_special_rid_target_mac(macs)) {
            std::fprintf(stderr,
                         "[wifi-rid-bridge] raw hex mac=%s len=%zu data=%s\n",
                         macs.c_str(),
                         raw_hex_payload.size() / 2,
                         raw_hex_payload.c_str());
        }
        SeenEntry &entry = seen[macs];
        if (entry.last_emit_ms > 0) {
            const double dt_s = std::max(0.001, (tnow - entry.last_emit_ms) / 1000.0);
            if (dt_s <= 20.0) {
                const double jump_m = geo::haversine_m(entry.last_lat, entry.last_lon,
                                                       drone_lat, drone_lon);
                const double max_jump_m = std::max(20.0, 45.0 * dt_s + 10.0);
                if (jump_m > max_jump_m) {
                    bool confirmed_reloc = false;
                    if (entry.pending_hits > 0 && (tnow - entry.pending_ms) <= 5000) {
                        const double pending_delta = geo::haversine_m(entry.pending_lat, entry.pending_lon,
                                                                       drone_lat, drone_lon);
                        if (pending_delta <= 20.0) {
                            entry.pending_lat = drone_lat;
                            entry.pending_lon = drone_lon;
                            entry.pending_hits++;
                            entry.pending_ms = tnow;
                            if (entry.pending_hits >= 2) {
                                confirmed_reloc = true;
                                entry.pending_hits = 0;
                            }
                        } else {
                            entry.pending_lat = drone_lat;
                            entry.pending_lon = drone_lon;
                            entry.pending_hits = 1;
                            entry.pending_ms = tnow;
                        }
                    } else {
                        entry.pending_lat = drone_lat;
                        entry.pending_lon = drone_lon;
                        entry.pending_hits = 1;
                        entry.pending_ms = tnow;
                    }

                    if (!confirmed_reloc) {
                        entry.rejected_jumps++;
                        if (entry.rejected_jumps <= 3 || (entry.rejected_jumps % 20) == 0) {
                            std::fprintf(stderr,
                                         "[wifi-rid-bridge] reject jump %s jump=%.1fm dt=%.2fs prev=(%.7f,%.7f) now=(%.7f,%.7f)\n",
                                         macs.c_str(), jump_m, dt_s,
                                         entry.last_lat, entry.last_lon,
                                         drone_lat, drone_lon);
                        }
                        stats.dropped++;
                        continue;
                    }
                    std::fprintf(stderr,
                                 "[wifi-rid-bridge] accept relocation %s after consistent samples\n",
                                 macs.c_str());
                }
            }
        }
        const bool tiny_motion = (std::abs(drone_lat - entry.last_lat) < 1e-7 &&
                                  std::abs(drone_lon - entry.last_lon) < 1e-7);
        if ((tnow - entry.last_emit_ms < args.min_emit_ms) && tiny_motion) {
            continue;
        }
        entry.last_emit_ms = tnow;
        entry.last_lat = drone_lat;
        entry.last_lon = drone_lon;

        const bool has_operator = (std::abs(operator_lat) > 1e-9 || std::abs(operator_lon) > 1e-9);
        const std::string json = emit_json(mac, obs_lat, obs_lon, drone_lat, drone_lon,
                           remote_id_id, operator_lat, operator_lon, has_operator);
        const ssize_t sent = sendto(udp, json.data(), json.size(), MSG_NOSIGNAL,
                                    (struct sockaddr *)&dst, sizeof(dst));
        if (sent < 0) {
            std::fprintf(stderr, "[wifi-rid-bridge] sendto failed: %s\n", std::strerror(errno));
            continue;
        }

        if (adaptive_hop && current_channel > 0) {
            if (locked_channel != current_channel) {
                std::fprintf(stderr,
                             "[wifi-rid-bridge] adaptive-hop lock channel %d (rid hit mac=%s)\n",
                             current_channel,
                             macs.c_str());
            }
            locked_channel = current_channel;
            locked_hit_since_hop = true;
        }
        
        /* Update last drone channel for weighted hopping */
        if (current_channel > 0 && g_last_drone_channel != current_channel) {
            g_last_drone_channel = current_channel;
            if (g_weighted_hop_external || g_fiveghz_interleave) {
                std::fprintf(stderr,
                             "[wifi-rid-bridge] updated drone channel: %d (for %s hopping)\n",
                             current_channel,
                             g_weighted_hop_external ? "weighted" : "5GHz-interleave");
            }
        }

        stats.loc++;
        if (g_bridge_role == ROLE_SEARCHER) {
            stats.searcher_hit++;
        }
        if (g_bridge_role == ROLE_TRACKER && g_tracker_followup_pending &&
            tnow <= g_tracker_followup_deadline_ms) {
            stats.tracker_followup_hit++;
            g_tracker_followup_pending = false;
        }
        /* Notify tracker of detected drone channel.
         * Searcher/standard: write IPC so tracker knows which channel to lock.
         * Tracker: also refresh IPC timestamp to self-reinforce the lock — prevents
         * the stale timer from firing while the tracker is actively receiving packets,
         * even if the searcher temporarily misses the same channel due to CRC errors. */
        if (current_channel > 0) {
            notify_tracker_channel(current_channel, frame_signal_dbm, args.iface);
        }
        std::fprintf(stderr,
                     "[wifi-rid-bridge] rid=1 %s lat=%.7f lon=%.7f dist=%.1fm brg=%.1f\n",
                     macs.c_str(), drone_lat, drone_lon,
                     geo::haversine_m(obs_lat, obs_lon, drone_lat, drone_lon),
                     geo::bearing_deg(obs_lat, obs_lon, drone_lat, drone_lon));
    }

    close(udp);
    close(rx);
    return 0;
}
