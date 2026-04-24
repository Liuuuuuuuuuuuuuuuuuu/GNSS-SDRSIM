#ifndef BRIDGE_SELFCHECK_HPP
#define BRIDGE_SELFCHECK_HPP

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include <net/if.h>

namespace bridge_check {

struct CheckResult {
    bool fatal = false;
    bool ok = false;
    std::string label;
    std::string detail;
};

struct RuntimeHealth {
    unsigned idle_windows = 0;
    unsigned no_signature_windows = 0;
    unsigned recovery_count = 0;
    long long last_recovery_ms = 0;
};

inline CheckResult make_result(bool fatal,
                               bool ok,
                               const std::string &label,
                               const std::string &detail)
{
    CheckResult result;
    result.fatal = fatal;
    result.ok = ok;
    result.label = label;
    result.detail = detail;
    return result;
}

inline CheckResult check_udp_port(int port)
{
    return make_result(true,
                       port > 0 && port <= 65535,
                       "udp-port",
                       port > 0 && port <= 65535 ? "valid" : "must be in the range 1..65535");
}

inline CheckResult check_coordinates(double lat, double lon)
{
    const bool ok = (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0);
    return make_result(true,
                       ok,
                       "observer-coordinates",
                       ok ? "valid" : "latitude or longitude is out of range");
}

inline CheckResult check_observer_cache_file()
{
    const bool ok = (access("/tmp/bds_sim_obs.json", R_OK) == 0);
    return make_result(false,
                       ok,
                       "observer-cache",
                       ok ? "found /tmp/bds_sim_obs.json" : "cache file is missing; built-in observer coordinates will be used");
}

inline CheckResult check_raw_capture_permissions(const char *what)
{
    const bool ok = (geteuid() == 0);
    return make_result(false,
                       ok,
                       what,
                       ok ? "running as root" : "not running as root; raw capture may fail without CAP_NET_RAW or CAP_NET_ADMIN");
}

inline CheckResult check_interface_exists(const std::string &iface)
{
    const bool ok = !iface.empty() && if_nametoindex(iface.c_str()) != 0;
    return make_result(true,
                       ok,
                       "interface",
                       ok ? iface : "network interface was not found");
}

inline CheckResult check_channel_hop_count(size_t count)
{
    const bool ok = count > 0;
    return make_result(true,
                       ok,
                       "channel-hop",
                       ok ? (std::to_string(count) + " channels configured") : "channel hop list is empty");
}

inline CheckResult check_hci_device(const std::string &requested_name, int dev_id)
{
    const bool ok = dev_id >= 0;
    return make_result(true,
                       ok,
                       "hci-device",
                       ok ? (requested_name + " resolved to hci" + std::to_string(dev_id))
                          : ("no HCI device could be resolved for " + requested_name));
}

inline bool print_report(const char *tag, const std::vector<CheckResult> &checks)
{
    bool fatal_failure = false;
    for (const CheckResult &check : checks) {
        const char *level = check.ok ? "ok" : (check.fatal ? "fail" : "warn");
        std::fprintf(stderr, "[%s] self-check %s: %s - %s\n",
                     tag, level, check.label.c_str(), check.detail.c_str());
        if (!check.ok && check.fatal) fatal_failure = true;
    }
    return !fatal_failure;
}

inline void note_recovery(RuntimeHealth &health, long long now_ms)
{
    health.recovery_count++;
    health.last_recovery_ms = now_ms;
}

inline void report_runtime_health(const char *tag,
                                  RuntimeHealth &health,
                                  uint64_t rx,
                                  uint64_t oui)
{
    if (rx == 0) {
        health.idle_windows++;
    } else {
        health.idle_windows = 0;
    }

    if (rx > 0 && oui == 0) {
        health.no_signature_windows++;
    } else if (oui > 0 || rx == 0) {
        health.no_signature_windows = 0;
    }

    if (health.idle_windows == 3 || (health.idle_windows > 0 && health.idle_windows % 10 == 0)) {
        std::fprintf(stderr,
                     "[%s] health warn: no packets received for %u consecutive seconds\n",
                     tag, health.idle_windows);
    }

    if (health.no_signature_windows == 5 || (health.no_signature_windows > 0 && health.no_signature_windows % 10 == 0)) {
        std::fprintf(stderr,
                     "[%s] health warn: packets are arriving, but no RID signature has matched for %u consecutive seconds\n",
                     tag, health.no_signature_windows);
    }
}

} /* namespace bridge_check */

#endif /* BRIDGE_SELFCHECK_HPP */