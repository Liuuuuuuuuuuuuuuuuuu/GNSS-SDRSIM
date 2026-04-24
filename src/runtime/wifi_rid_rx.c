#include "wifi_rid_rx.h"

#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t g_wifi_pid = -1;
static pid_t g_wifi_pid_2 = -1;
static pid_t g_wifi_pid_3 = -1;
static pid_t g_wifi_pid_4 = -1;

static void kill_bridge_by_name(const char *name) {
    if (!name || !name[0]) return;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "pkill -TERM -f '%s' >/dev/null 2>&1 || true", name);
    int rc = system(cmd);
    (void)rc;
    usleep(50000);
    snprintf(cmd, sizeof(cmd), "pkill -KILL -f '%s' >/dev/null 2>&1 || true", name);
    rc = system(cmd);
    (void)rc;
}

static void copy_cstr(char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int str_truthy(const char *s, int default_value) {
    if (!s || !s[0]) return default_value;
    if (strcmp(s, "0") == 0) return 0;
    if (strcasecmp(s, "false") == 0) return 0;
    if (strcasecmp(s, "no") == 0) return 0;
    return 1;
}

static int detect_wifi_iface(char *out, size_t out_sz) {
    if (!out || out_sz == 0) return 0;

    /* First choice: any interface already in monitor mode. */
    FILE *pp_mon = popen("iw dev 2>/dev/null | awk '$1==\"Interface\"{i=$2} $1==\"type\" && $2==\"monitor\"{print i; exit}'", "r");
    if (pp_mon) {
        char line[128] = {0};
        if (fgets(line, sizeof(line), pp_mon)) {
            pclose(pp_mon);
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0]) {
                copy_cstr(out, out_sz, line);
                return 1;
            }
        } else {
            pclose(pp_mon);
        }
    }

    /* Prefer known external sniffing chipsets: RTL8187 and RT3070/2870 class USB adapters. */
    DIR *d = opendir("/sys/class/net");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *ifn = de->d_name;
            if (strcmp(ifn, ".") == 0 || strcmp(ifn, "..") == 0 || strcmp(ifn, "lo") == 0) {
                continue;
            }
            {
                size_t n = strlen(ifn);
                if (n >= 3 && strcmp(ifn + n - 3, "mon") == 0) {
                    continue; /* skip monitor interfaces */
                }
            }

            char path[512];
            snprintf(path, sizeof(path), "/sys/class/net/%s/device/modalias", ifn);
            FILE *f = fopen(path, "r");
            if (!f) continue;

            char modalias[512] = {0};
            if (!fgets(modalias, sizeof(modalias), f)) {
                fclose(f);
                continue;
            }
            fclose(f);
            if (strstr(modalias, "v0BDAp8187") != NULL ||
                strstr(modalias, "v148Fp3070") != NULL ||
                strstr(modalias, "v148Fp2870") != NULL) {
                copy_cstr(out, out_sz, ifn);
                closedir(d);
                return 1;
            }
        }
        closedir(d);
    }

    /* Fallback: prefer managed interface from iw dev */
    FILE *pp = popen("iw dev 2>/dev/null | awk '$1==\"Interface\"{i=$2} $1==\"type\" && $2==\"managed\"{print i; exit}'", "r");
    if (!pp) return 0;
    char line[128] = {0};
    if (!fgets(line, sizeof(line), pp)) {
        pclose(pp);
        return 0;
    }
    pclose(pp);
    line[strcspn(line, "\r\n")] = '\0';
    if (!line[0]) return 0;
    copy_cstr(out, out_sz, line);
    return 1;
}

static void try_auto_setup_monitor_iface(char *iface, size_t iface_sz) {
    if (!iface || !iface[0]) return;
    {
        size_t n = strlen(iface);
        if (n >= 3 && strcmp(iface + n - 3, "mon") == 0) {
            return; /* already monitor-like iface name */
        }
    }
    if (!str_truthy(getenv("BDS_WIFI_RID_AUTO_SETUP"), 1)) return;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "sudo -n scripts/wifi/setup_alfa_awus046nh.sh %s 2>/dev/null", iface);
    FILE *pp = popen(cmd, "r");
    if (!pp) return;

    char line[512];
    while (fgets(line, sizeof(line), pp)) {
        if (strncmp(line, "export BDS_WIFI_RID_IFACE=", 26) == 0) {
            char *v = line + 26;
            v[strcspn(v, "\r\n")] = '\0';
            if (v[0]) {
                copy_cstr(iface, iface_sz, v);
            }
            break;
        }
    }
    (void)pclose(pp);
}

static int iface_exists(const char *ifn) {
    if (!ifn || !ifn[0]) return 0;
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s", ifn);
    return access(path, F_OK) == 0;
}

static int file_executable(const char *path) {
    if (!path || !path[0]) return 0;
    return access(path, X_OK) == 0;
}

static const char *wifi_rid_sudo_preserve_env(void)
{
    return "--preserve-env=BDS_WIFI_RID_WIFI_MODE,BDS_WIFI_RID_RAW_HEX,BDS_WIFI_RID_HOP_MS,BDS_WIFI_RID_CHANNELS,BDS_WIFI_RID_ALLOW_IDS,BDS_WIFI_RID_BLOCK_IDS,BDS_WIFI_RID_ADAPTIVE_HOP";
}

static int command_exists(const char *cmd) {
    if (!cmd || !cmd[0]) return 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
}

static void normalize_monitor_iface_name(char *iface, size_t iface_sz) {
    if (!iface || !iface[0] || iface_sz == 0) return;

    /* If user/env passed xxxmonmon, prefer xxxmon when it exists. */
    size_t n = strlen(iface);
    if (n >= 6 && strcmp(iface + n - 6, "monmon") == 0) {
        char cand[128] = {0};
        size_t base_len = n - 3; /* drop only one trailing "mon" */
        if (base_len >= sizeof(cand)) base_len = sizeof(cand) - 1;
        memcpy(cand, iface, base_len);
        cand[base_len] = '\0';
        if (iface_exists(cand)) {
            copy_cstr(iface, iface_sz, cand);
            return;
        }
    }

    /* If iface is managed name and iface+mon exists, prefer monitor iface. */
    char mon[128] = {0};
    if (snprintf(mon, sizeof(mon), "%smon", iface) > 0 && iface_exists(mon)) {
        size_t m = strlen(iface);
        if (!(m >= 3 && strcmp(iface + m - 3, "mon") == 0)) {
            copy_cstr(iface, iface_sz, mon);
        }
    }
}

static int child_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return (errno == EPERM) ? 1 : 0;
}

static int ensure_sudo_credentials(const char *tag) {
    int rc = system("sudo -n -v >/dev/null 2>&1");
    if (rc == 0) return 0;

    fprintf(stderr,
            "[%s] sudo credential missing; requesting password once for background bridge startup...\n",
            tag ? tag : "wifi_rid");
    rc = system("sudo -v");
    if (rc != 0) {
        fprintf(stderr,
                "[%s] sudo pre-auth failed; bridge will not start.\n",
                tag ? tag : "wifi_rid");
        return -1;
    }
    return 0;
}

int wifi_rid_is_active(void) {
    int active = 0;
    if (g_wifi_pid > 0 && child_alive(g_wifi_pid)) {
        active = 1;
    } else {
        g_wifi_pid = -1;
    }
    if (g_wifi_pid_2 > 0 && child_alive(g_wifi_pid_2)) {
        active = 1;
    } else {
        g_wifi_pid_2 = -1;
    }
    if (g_wifi_pid_3 > 0 && child_alive(g_wifi_pid_3)) {
        active = 1;
    } else {
        g_wifi_pid_3 = -1;
    }
    if (g_wifi_pid_4 > 0 && child_alive(g_wifi_pid_4)) {
        active = 1;
    } else {
        g_wifi_pid_4 = -1;
    }
    return active;
}

void wifi_rid_set_observer_pos(double lat_deg, double lon_deg) {
    /* Write observer position atomically via rename to avoid partial reads */
    const char *tmp = "/tmp/bds_sim_obs.json.tmp";
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"lat\":%.9f,\"lon\":%.9f}\n", lat_deg, lon_deg);
    fclose(f);
    rename(tmp, "/tmp/bds_sim_obs.json");
}

/* Helper function to start a single Wi-Fi bridge on a specified interface */
static int wifi_rid_start_single_iface(const char *raw_iface, int interface_num,
                                        const char *role_override,
                                        const char *channels_override) {
    if (!raw_iface || !raw_iface[0]) return -1;
    
    char iface_buf[128] = {0};
    copy_cstr(iface_buf, sizeof(iface_buf), raw_iface);
    try_auto_setup_monitor_iface(iface_buf, sizeof(iface_buf));
    normalize_monitor_iface_name(iface_buf, sizeof(iface_buf));
    const char *iface = iface_buf;

    const char *bridge_bin = getenv("BDS_WIFI_RID_BRIDGE_BIN");
    if (!bridge_bin || !bridge_bin[0]) {
        const char *source = getenv("BDS_WIFI_RID_SOURCE");
        const int prefer_wireshark = (source &&
                                      (strcasecmp(source, "wireshark") == 0 ||
                                       strcasecmp(source, "wireshart") == 0 ||
                                       strcasecmp(source, "tshark") == 0));
        const int force_legacy = (source &&
                                  (strcasecmp(source, "legacy") == 0 ||
                                   strcasecmp(source, "raw") == 0 ||
                                   strcasecmp(source, "pcap") == 0 ||
                                   strcasecmp(source, "bridge") == 0));
        const int source_auto = (!source || !source[0] || strcasecmp(source, "auto") == 0);
        const int has_tshark_bridge = file_executable("bin/wifi-rid-tshark-bridge");
        const int has_tshark = command_exists("tshark");

        if (prefer_wireshark) {
            if (has_tshark_bridge && has_tshark) {
                bridge_bin = "bin/wifi-rid-tshark-bridge";
            } else {
                fprintf(stderr,
                        "[wifi_rid] BDS_WIFI_RID_SOURCE=%s requires tshark + bin/wifi-rid-tshark-bridge; strict mode abort\n",
                        source);
                return -1;
            }
        } else if (force_legacy) {
            bridge_bin = "bin/wifi-rid-bridge";
        } else if (has_tshark_bridge && has_tshark) {
            bridge_bin = "bin/wifi-rid-tshark-bridge";
            if (source_auto) {
                fprintf(stderr,
                        "[wifi_rid] default source: wireshark (set BDS_WIFI_RID_SOURCE=legacy to force old bridge)\n");
            }
        } else {
            bridge_bin = "bin/wifi-rid-bridge";
            if (source_auto) {
                fprintf(stderr,
                        "[wifi_rid] wireshark path unavailable, fallback to legacy bridge (missing tshark or tshark bridge binary)\n");
            }
        }
    }
    if (!file_executable(bridge_bin)) {
        fprintf(stderr,
                "[wifi_rid] bridge binary not found (%s); Wi-Fi RID bridge disabled.\n",
                bridge_bin);
        return -1;
    }
    const int supports_role_channels = (strstr(bridge_bin, "wifi-rid-tshark-bridge") == NULL);

    const char *use_sudo = getenv("BDS_WIFI_RID_USE_SUDO");
    const int want_sudo = str_truthy(use_sudo, 1);
    const char *sudo_nonint = getenv("BDS_WIFI_RID_SUDO_NONINTERACTIVE");
    const int use_sudo_noninteractive = str_truthy(sudo_nonint, 0);

    if (want_sudo && interface_num == 1) {
        if (ensure_sudo_credentials("wifi_rid") != 0) {
            return -1;
        }
    }

    const char *udp_port = getenv("BDS_WIFI_RID_UDP_PORT");
    if (!udp_port || !udp_port[0]) {
        udp_port = "39001";
    }
    const char *allow_ids = getenv("BDS_WIFI_RID_ALLOW_IDS");
    const int has_allow_ids = (allow_ids && allow_ids[0]);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[wifi_rid] fork failed: %s\n", strerror(errno));
        return -1;
    }

    char obs_lat_str[32] = {0};
    char obs_lon_str[32] = {0};
    const char *obs_lat_env = getenv("BDS_OBS_LAT");
    const char *obs_lon_env = getenv("BDS_OBS_LON");
    int has_obs = 0;
    if (obs_lat_env && obs_lat_env[0] && obs_lon_env && obs_lon_env[0]) {
        snprintf(obs_lat_str, sizeof(obs_lat_str), "%s", obs_lat_env);
        snprintf(obs_lon_str, sizeof(obs_lon_str), "%s", obs_lon_env);
        has_obs = 1;
    }

    if (pid == 0) {
        /* Build argv for execvp: handles sudo, obs coords, allow-ids, role, channels */
        const char *exec_argv[40];
        int n = 0;
        const char *exec_prog;
        if (want_sudo) {
            exec_prog = "sudo";
            exec_argv[n++] = "sudo";
            exec_argv[n++] = wifi_rid_sudo_preserve_env();
            if (use_sudo_noninteractive) exec_argv[n++] = "-n";
            exec_argv[n++] = bridge_bin;
        } else {
            exec_prog = bridge_bin;
            exec_argv[n++] = bridge_bin;
        }
        exec_argv[n++] = "--iface";
        exec_argv[n++] = iface;
        exec_argv[n++] = "--udp-port";
        exec_argv[n++] = udp_port;
        if (has_obs) {
            exec_argv[n++] = "--obs-lat";
            exec_argv[n++] = obs_lat_str;
            exec_argv[n++] = "--obs-lon";
            exec_argv[n++] = obs_lon_str;
        }
        if (has_allow_ids) {
            exec_argv[n++] = "--allow-ids";
            exec_argv[n++] = allow_ids;
        }
        if (supports_role_channels && role_override && role_override[0]) {
            exec_argv[n++] = "--role";
            exec_argv[n++] = role_override;
        }
        if (supports_role_channels && channels_override && channels_override[0]) {
            exec_argv[n++] = "--channels";
            exec_argv[n++] = channels_override;
        }
        exec_argv[n] = NULL;
        execvp(exec_prog, (char * const *)exec_argv);
        fprintf(stderr, "[wifi_rid] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* Parent: verify child startup */
    usleep(200000);
    {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            fprintf(stderr,
                    "[wifi_rid] bridge launch failed (iface=%s); child exited immediately.\n",
                    iface);
            return -1;
        }
    }

    /* Store PID based on interface number */
    if (interface_num == 1) {
        g_wifi_pid = pid;
    } else if (interface_num == 2) {
        g_wifi_pid_2 = pid;
    } else if (interface_num == 3) {
        g_wifi_pid_3 = pid;
    } else if (interface_num == 4) {
        g_wifi_pid_4 = pid;
    }
    
    fprintf(stderr,
            "[wifi_rid] started bridge pid=%d iface=%s (interface #%d)\n",
            (int)pid, iface, interface_num);
    return 0;
}

int wifi_rid_start_from_env(void) {
    /* Check for multi-interface config first (IFACE_1, IFACE_2) */
    const char *iface_1_env = getenv("BDS_WIFI_RID_IFACE_1");
    const char *iface_2_env = getenv("BDS_WIFI_RID_IFACE_2");
    
    if (iface_1_env && iface_1_env[0]) {
        /* Multi-interface mode: Search & Track architecture */
        const char *iface_3_env = getenv("BDS_WIFI_RID_IFACE_3");
        const char *iface_4_env = getenv("BDS_WIFI_RID_IFACE_4");
        const int three_nic = (iface_3_env && iface_3_env[0]);
        const int four_nic  = three_nic && (iface_4_env && iface_4_env[0]);

        /* Channel defaults per NIC count.
         * 4-NIC: IFACE_1 ch1,6,11 | IFACE_2 ch2-5,7 (searchers) | IFACE_3 tracker | IFACE_4 aux ch8-10,12,13
         * 3-NIC: IFACE_1 full 1-13 (searcher) | IFACE_2 tracker | IFACE_3 aux searcher */
        const char *ch1 = getenv("BDS_WIFI_RID_IFACE_1_CHANNELS");
        const char *ch2 = getenv("BDS_WIFI_RID_IFACE_2_CHANNELS");
        const char *ch4 = getenv("BDS_WIFI_RID_IFACE_4_CHANNELS");
        if (!ch1 || !ch1[0]) ch1 = four_nic ? "1,6,11" : NULL;     /* 3-NIC: NULL = full 1-13 hop */
        if (!ch2 || !ch2[0]) ch2 = four_nic ? "2,3,4,5,7" : NULL;  /* 3-NIC: IFACE_2 is tracker */
        if (!ch4 || !ch4[0]) ch4 = four_nic ? "8,9,10,12,13" : NULL;

        fprintf(stderr,
                "[wifi_rid] multi-interface mode: IFACE_1=%s IFACE_2=%s%s%s%s%s\n",
                iface_1_env,
                iface_2_env ? iface_2_env : "(not set)",
                three_nic ? " IFACE_3=" : "",
                three_nic ? iface_3_env : "",
                four_nic  ? " IFACE_4=" : "",
                four_nic  ? iface_4_env : "");

        /* IFACE_1: searcher — full 1-13 scan in 3-NIC, ch1,6,11 in 4-NIC */
        if (wifi_rid_start_single_iface(iface_1_env, 1, "searcher", ch1) < 0) {
            return -1;
        }
        /* IFACE_2: tracker in 3-NIC, searcher in 4-NIC */
        if (iface_2_env && iface_2_env[0]) {
            const char *role2   = four_nic ? "searcher" : "tracker";
            const char *ch2_arg = four_nic ? ch2 : NULL;
            if (wifi_rid_start_single_iface(iface_2_env, 2, role2, ch2_arg) < 0) {
                fprintf(stderr, "[wifi_rid] second interface startup failed; stopping all bridges.\n");
                wifi_rid_stop();
                return -1;
            }
        }
        /* IFACE_3: tracker in 4-NIC, aux searcher in 3-NIC (non-fatal in both) */
        if (three_nic) {
            const char *role3 = four_nic ? "tracker" : "searcher";
            if (wifi_rid_start_single_iface(iface_3_env, 3, role3, NULL) < 0) {
                fprintf(stderr,
                        "[wifi_rid] %s interface startup failed; continuing without it.\n",
                        four_nic ? "tracker" : "aux");
                /* Non-fatal */
            }
        }
        /* IFACE_4: aux searcher (4-NIC only, non-fatal) */
        if (four_nic) {
            if (wifi_rid_start_single_iface(iface_4_env, 4, "searcher", ch4) < 0) {
                fprintf(stderr,
                        "[wifi_rid] auxiliary interface startup failed; continuing without it.\n");
                /* Non-fatal */
            }
        }
        return 0;
    }
    
    /* Single interface mode (original logic) */
    char iface_buf[128] = {0};
    const char *iface_env = getenv("BDS_WIFI_RID_IFACE");
    if (iface_env && iface_env[0]) {
        copy_cstr(iface_buf, sizeof(iface_buf), iface_env);
    } else if (!detect_wifi_iface(iface_buf, sizeof(iface_buf))) {
        return 0; /* no Wi-Fi interface found */
    }

    try_auto_setup_monitor_iface(iface_buf, sizeof(iface_buf));
    normalize_monitor_iface_name(iface_buf, sizeof(iface_buf));
    const char *iface = iface_buf;

    if (wifi_rid_is_active()) {
        return 0;
    }

    int strict_wireshark = 0;
    const char *bridge_bin = getenv("BDS_WIFI_RID_BRIDGE_BIN");
    if (!bridge_bin || !bridge_bin[0]) {
        const char *source = getenv("BDS_WIFI_RID_SOURCE");
        const int prefer_wireshark = (source &&
                                      (strcasecmp(source, "wireshark") == 0 ||
                                       strcasecmp(source, "wireshart") == 0 ||
                                       strcasecmp(source, "tshark") == 0));
        const int force_legacy = (source &&
                                  (strcasecmp(source, "legacy") == 0 ||
                                   strcasecmp(source, "raw") == 0 ||
                                   strcasecmp(source, "pcap") == 0 ||
                                   strcasecmp(source, "bridge") == 0));
        const int source_auto = (!source || !source[0] || strcasecmp(source, "auto") == 0);
        const int has_tshark_bridge = file_executable("bin/wifi-rid-tshark-bridge");
        const int has_tshark = command_exists("tshark");

        if (prefer_wireshark) {
            strict_wireshark = 1;
            if (has_tshark_bridge && has_tshark) {
                bridge_bin = "bin/wifi-rid-tshark-bridge";
            } else {
                fprintf(stderr,
                        "[wifi_rid] BDS_WIFI_RID_SOURCE=%s requires tshark + bin/wifi-rid-tshark-bridge; strict mode abort\n",
                        source);
                return -1;
            }
        } else if (force_legacy) {
            bridge_bin = "bin/wifi-rid-bridge";
        } else if (has_tshark_bridge && has_tshark) {
            bridge_bin = "bin/wifi-rid-tshark-bridge";
            if (source_auto) {
                fprintf(stderr,
                        "[wifi_rid] default source: wireshark (set BDS_WIFI_RID_SOURCE=legacy to force old bridge)\n");
            }
        } else {
            bridge_bin = "bin/wifi-rid-bridge";
            if (source_auto) {
                fprintf(stderr,
                        "[wifi_rid] wireshark path unavailable, fallback to legacy bridge (missing tshark or tshark bridge binary)\n");
            }
        }
    }
    if (!file_executable(bridge_bin)) {
        fprintf(stderr,
                "[wifi_rid] bridge binary not found (%s); Wi-Fi RID bridge disabled.\n"
                "[wifi_rid] This C/C++ package does not require Python tools.\n",
                bridge_bin);
        return 0;
    }
    if (strict_wireshark) {
        fprintf(stderr, "[wifi_rid] selected bridge: %s (strict wireshark mode)\n", bridge_bin);
    } else {
        fprintf(stderr, "[wifi_rid] selected bridge: %s\n", bridge_bin);
    }

    const char *use_sudo = getenv("BDS_WIFI_RID_USE_SUDO");
    const int want_sudo = str_truthy(use_sudo, 1);
    const char *sudo_nonint = getenv("BDS_WIFI_RID_SUDO_NONINTERACTIVE");
    const int use_sudo_noninteractive = str_truthy(sudo_nonint, 0);

    if (want_sudo) {
        if (ensure_sudo_credentials("wifi_rid") != 0) {
            return -1;
        }
    }

    const char *udp_port = getenv("BDS_WIFI_RID_UDP_PORT");
    if (!udp_port || !udp_port[0]) {
        udp_port = "39001";
    }
    const char *allow_ids = getenv("BDS_WIFI_RID_ALLOW_IDS");
    const int has_allow_ids = (allow_ids && allow_ids[0]);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[wifi_rid] fork failed: %s\n", strerror(errno));
        return -1;
    }

    /* Build obs-lat / obs-lon string args (may be empty if pos not yet known) */
    char obs_lat_str[32] = {0};
    char obs_lon_str[32] = {0};
    const char *obs_lat_env = getenv("BDS_OBS_LAT");
    const char *obs_lon_env = getenv("BDS_OBS_LON");
    int has_obs = 0;
    if (obs_lat_env && obs_lat_env[0] && obs_lon_env && obs_lon_env[0]) {
        snprintf(obs_lat_str, sizeof(obs_lat_str), "%s", obs_lat_env);
        snprintf(obs_lon_str, sizeof(obs_lon_str), "%s", obs_lon_env);
        has_obs = 1;
    }

    if (pid == 0) {
        if (has_obs) {
            if (want_sudo) {
                if (use_sudo_noninteractive) {
                    if (has_allow_ids) {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), "-n", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                               "--allow-ids", allow_ids,
                               (char *)NULL);
                    } else {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), "-n", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                               (char *)NULL);
                    }
                } else {
                    if (has_allow_ids) {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                               "--allow-ids", allow_ids,
                               (char *)NULL);
                    } else {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                               (char *)NULL);
                    }
                }
            }
            if (has_allow_ids) {
                execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                       "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                       "--allow-ids", allow_ids,
                       (char *)NULL);
            } else {
                execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                       "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                       (char *)NULL);
            }
        } else {
            if (want_sudo) {
                if (use_sudo_noninteractive) {
                    if (has_allow_ids) {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), "-n", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--allow-ids", allow_ids,
                               (char *)NULL);
                    } else {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), "-n", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port, (char *)NULL);
                    }
                } else {
                    if (has_allow_ids) {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--allow-ids", allow_ids,
                               (char *)NULL);
                    } else {
                           execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                               "--iface", iface, "--udp-port", udp_port, (char *)NULL);
                    }
                }
            }
            if (has_allow_ids) {
                execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                       "--allow-ids", allow_ids,
                       (char *)NULL);
            } else {
                execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                       (char *)NULL);
            }
        }
        fprintf(stderr, "[wifi_rid] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* Verify child did not exit immediately (e.g. sudo -n credential failure). */
    usleep(200000);
    {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            if (want_sudo && use_sudo_noninteractive) {
                fprintf(stderr,
                        "[wifi_rid] sudo -n launch failed; retrying with interactive sudo prompt...\n");

                pid_t pid2 = fork();
                if (pid2 < 0) {
                    fprintf(stderr, "[wifi_rid] fork failed (retry): %s\n", strerror(errno));
                    return -1;
                }

                if (pid2 == 0) {
                    if (has_obs) {
                        if (has_allow_ids) {
                            execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                                   "--iface", iface, "--udp-port", udp_port,
                                   "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                                   "--allow-ids", allow_ids,
                                   (char *)NULL);
                        } else {
                            execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                                   "--iface", iface, "--udp-port", udp_port,
                                   "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                                   (char *)NULL);
                        }
                    } else {
                        if (has_allow_ids) {
                            execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                                   "--iface", iface, "--udp-port", udp_port,
                                   "--allow-ids", allow_ids,
                                   (char *)NULL);
                        } else {
                            execlp("sudo", "sudo", wifi_rid_sudo_preserve_env(), bridge_bin,
                                   "--iface", iface, "--udp-port", udp_port, (char *)NULL);
                        }
                    }
                    fprintf(stderr, "[wifi_rid] exec failed (retry): %s\n", strerror(errno));
                    _exit(127);
                }

                usleep(200000);
                {
                    int st2 = 0;
                    pid_t rc2 = waitpid(pid2, &st2, WNOHANG);
                    if (rc2 == pid2) {
                        fprintf(stderr,
                                "[wifi_rid] bridge launch failed after interactive retry (iface=%s).\n",
                                iface);
                        return -1;
                    }
                }

                g_wifi_pid = pid2;
                fprintf(stderr,
                        "[wifi_rid] started bridge pid=%d iface=%s (sudo=on, noninteractive=off)\n",
                        (int)pid2,
                        iface);
                return 0;
            }

            fprintf(stderr,
                    "[wifi_rid] bridge launch failed (iface=%s). "
                    "Hint: run 'sudo -v' before ./bds-sim, or set "
                    "BDS_WIFI_RID_SUDO_NONINTERACTIVE=0 for interactive sudo prompt.\n",
                    iface);
            return -1;
        }
    }

    g_wifi_pid = pid;
    fprintf(stderr,
            "[wifi_rid] started bridge pid=%d iface=%s (sudo=%s, noninteractive=%s)\n",
            (int)pid,
            iface,
            want_sudo ? "on" : "off",
            use_sudo_noninteractive ? "on" : "off");
    return 0;
}

void wifi_rid_stop(void) {
    if (g_wifi_pid > 0) {
        pid_t pid = g_wifi_pid;
        g_wifi_pid = -1;
        if (child_alive(pid)) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20; i++) {
                int status = 0;
                pid_t rc = waitpid(pid, &status, WNOHANG);
                if (rc == pid) break;
                usleep(50000);
            }
            if (child_alive(pid)) {
                kill(pid, SIGKILL);
                (void)waitpid(pid, NULL, 0);
            }
        }
    }
    if (g_wifi_pid_2 > 0) {
        pid_t pid = g_wifi_pid_2;
        g_wifi_pid_2 = -1;
        if (child_alive(pid)) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20; i++) {
                int status = 0;
                pid_t rc = waitpid(pid, &status, WNOHANG);
                if (rc == pid) break;
                usleep(50000);
            }
            if (child_alive(pid)) {
                kill(pid, SIGKILL);
                (void)waitpid(pid, NULL, 0);
            }
        }
    }
    if (g_wifi_pid_3 > 0) {
        pid_t pid = g_wifi_pid_3;
        g_wifi_pid_3 = -1;
        if (child_alive(pid)) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20; i++) {
                int status = 0;
                pid_t rc = waitpid(pid, &status, WNOHANG);
                if (rc == pid) break;
                usleep(50000);
            }
            if (child_alive(pid)) {
                kill(pid, SIGKILL);
                (void)waitpid(pid, NULL, 0);
            }
        }
    }
    if (g_wifi_pid_4 > 0) {
        pid_t pid = g_wifi_pid_4;
        g_wifi_pid_4 = -1;
        if (child_alive(pid)) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20; i++) {
                int status = 0;
                pid_t rc = waitpid(pid, &status, WNOHANG);
                if (rc == pid) break;
                usleep(50000);
            }
            if (child_alive(pid)) {
                kill(pid, SIGKILL);
                (void)waitpid(pid, NULL, 0);
            }
        }
    }

    // Some sudo launch paths can detach bridge worker from the tracked pid.
    // Sweep by executable name to guarantee bridge shutdown on GUI exit.
    kill_bridge_by_name("[w]ifi-rid-tshark-bridge");
    kill_bridge_by_name("[w]ifi-rid-bridge");
}

void wifi_rid_stop_fast(void) {
    if (g_wifi_pid > 0) {
        kill(g_wifi_pid, SIGKILL);
        g_wifi_pid = -1;
    }
    if (g_wifi_pid_2 > 0) {
        kill(g_wifi_pid_2, SIGKILL);
        g_wifi_pid_2 = -1;
    }
    if (g_wifi_pid_3 > 0) {
        kill(g_wifi_pid_3, SIGKILL);
        g_wifi_pid_3 = -1;
    }
    if (g_wifi_pid_4 > 0) {
        kill(g_wifi_pid_4, SIGKILL);
        g_wifi_pid_4 = -1;
    }

    /* Kill any detached workers left behind by sudo launch paths. */
    int rc = system("pkill -KILL -f '[w]ifi-rid-tshark-bridge' >/dev/null 2>&1 || true");
    (void)rc;
    rc = system("pkill -KILL -f '[w]ifi-rid-bridge' >/dev/null 2>&1 || true");
    (void)rc;
}
