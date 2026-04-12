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

    /* Prefer ALFA AWUS046NH chipset: USB VID:PID 0bda:8187 */
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
            if (strstr(modalias, "v0BDAp8187") != NULL) {
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

int wifi_rid_is_active(void) {
    if (g_wifi_pid <= 0) return 0;
    if (!child_alive(g_wifi_pid)) {
        g_wifi_pid = -1;
        return 0;
    }
    return 1;
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

int wifi_rid_start_from_env(void) {
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

    const char *bridge_bin = getenv("BDS_WIFI_RID_BRIDGE_BIN");
    if (!bridge_bin || !bridge_bin[0]) {
        bridge_bin = "bin/wifi-rid-bridge";
    }
    if (!file_executable(bridge_bin)) {
        fprintf(stderr,
                "[wifi_rid] bridge binary not found (%s); Wi-Fi RID bridge disabled.\n"
                "[wifi_rid] This C/C++ package does not require Python tools.\n",
                bridge_bin);
        return 0;
    }

    const char *use_sudo = getenv("BDS_WIFI_RID_USE_SUDO");
    const int want_sudo = str_truthy(use_sudo, 1);
    const char *sudo_nonint = getenv("BDS_WIFI_RID_SUDO_NONINTERACTIVE");
    const int use_sudo_noninteractive = str_truthy(sudo_nonint, 1);

    const char *udp_port = getenv("BDS_WIFI_RID_UDP_PORT");
    if (!udp_port || !udp_port[0]) {
        udp_port = "39001";
    }

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
                    execlp("sudo", "sudo", "-n", bridge_bin,
                           "--iface", iface, "--udp-port", udp_port,
                           "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                           (char *)NULL);
                } else {
                    execlp("sudo", "sudo", bridge_bin,
                           "--iface", iface, "--udp-port", udp_port,
                           "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                           (char *)NULL);
                }
            }
            execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                   "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                   (char *)NULL);
        } else {
            if (want_sudo) {
                if (use_sudo_noninteractive) {
                    execlp("sudo", "sudo", "-n", bridge_bin,
                           "--iface", iface, "--udp-port", udp_port, (char *)NULL);
                } else {
                    execlp("sudo", "sudo", bridge_bin,
                           "--iface", iface, "--udp-port", udp_port, (char *)NULL);
                }
            }
            execlp(bridge_bin, bridge_bin, "--iface", iface, "--udp-port", udp_port,
                   (char *)NULL);
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
                        execlp("sudo", "sudo", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str, "--obs-lon", obs_lon_str,
                               (char *)NULL);
                    } else {
                        execlp("sudo", "sudo", bridge_bin,
                               "--iface", iface, "--udp-port", udp_port, (char *)NULL);
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
    if (g_wifi_pid <= 0) return;

    pid_t pid = g_wifi_pid;
    g_wifi_pid = -1;

    if (!child_alive(pid)) return;

    kill(pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) return;
        usleep(50000);
    }

    kill(pid, SIGKILL);
    (void)waitpid(pid, NULL, 0);
}
