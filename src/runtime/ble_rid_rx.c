#include "ble_rid_rx.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t g_ble_pid = -1;

/* -------------------------------------------------------------------------- */
/* Internal helpers (mirrors wifi_rid_rx.c)                                   */
/* -------------------------------------------------------------------------- */

static void copy_cstr(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int str_truthy(const char *s, int default_value)
{
    if (!s || !s[0]) return default_value;
    if (strcmp(s, "0") == 0)         return 0;
    if (strcasecmp(s, "false") == 0) return 0;
    if (strcasecmp(s, "no") == 0)    return 0;
    return 1;
}

static int file_executable(const char *path)
{
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (st.st_mode & S_IXUSR) ? 1 : 0;
}

static int child_alive(pid_t pid)
{
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return (errno == EPERM) ? 1 : 0;
}

static int ensure_sudo_credentials(const char *tag)
{
    int rc = system("sudo -n -v >/dev/null 2>&1");
    if (rc == 0) return 0;

    fprintf(stderr,
        "[%s] sudo credential missing; requesting password once for background bridge startup...\n",
        tag ? tag : "ble_rid");
    rc = system("sudo -v");
    if (rc != 0) {
        fprintf(stderr,
            "[%s] sudo pre-auth failed; bridge will not start.\n",
            tag ? tag : "ble_rid");
        return -1;
    }
    return 0;
}

/* Check whether a Bluetooth HCI device exists in sysfs */
static int detect_hci_dev(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return 0;

    /* Try up to hci7 */
    char sysfs[64];
    for (int i = 0; i <= 7; ++i) {
        snprintf(sysfs, sizeof(sysfs), "/sys/class/bluetooth/hci%d", i);
        struct stat st;
        if (stat(sysfs, &st) == 0) {
            snprintf(out, out_sz, "hci%d", i);
            return 1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int ble_rid_is_active(void)
{
    if (g_ble_pid <= 0) return 0;
    if (!child_alive(g_ble_pid)) {
        g_ble_pid = -1;
        return 0;
    }
    return 1;
}

void ble_rid_set_observer_pos(double lat_deg, double lon_deg)
{
    /* Shared file with Wi-Fi bridge — either bridge reading is acceptable. */
    const char *tmp = "/tmp/bds_sim_obs.json.tmp";
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\"lat\":%.9f,\"lon\":%.9f}\n", lat_deg, lon_deg);
    fclose(f);
    rename(tmp, "/tmp/bds_sim_obs.json");
}

int ble_rid_start_from_env(void)
{
    char hci_buf[64] = {0};

    const char *hci_env = getenv("BDS_BLE_RID_HCI");
    if (hci_env && hci_env[0]) {
        copy_cstr(hci_buf, sizeof(hci_buf), hci_env);
    } else if (!detect_hci_dev(hci_buf, sizeof(hci_buf))) {
        return 0; /* no HCI device found */
    }

    if (ble_rid_is_active()) return 0;

    const char *bridge_bin = getenv("BDS_BLE_RID_BRIDGE_BIN");
    if (!bridge_bin || !bridge_bin[0]) {
        bridge_bin = "bin/ble-rid-bridge";
    }
    if (!file_executable(bridge_bin)) {
        fprintf(stderr,
            "[ble_rid] bridge binary not found (%s); BLE RID bridge disabled.\n",
            bridge_bin);
        return 0;
    }

    const char *use_sudo_env = getenv("BDS_BLE_RID_USE_SUDO");
    const int want_sudo = str_truthy(use_sudo_env, 1); /* default: use sudo */

    const char *sudo_nonint_env = getenv("BDS_BLE_RID_SUDO_NONINTERACTIVE");
    const int use_nonint = str_truthy(sudo_nonint_env, 1); /* default: -n */

    if (want_sudo) {
        if (ensure_sudo_credentials("ble_rid") != 0) {
            return -1;
        }
    }

    const char *udp_port = getenv("BDS_BLE_RID_UDP_PORT");
    if (!udp_port || !udp_port[0]) {
        udp_port = getenv("BDS_WIFI_RID_UDP_PORT"); /* share port env if set */
    }
    if (!udp_port || !udp_port[0]) {
        udp_port = "39001";
    }

    /* Build optional observer lat/lon string arguments */
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

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[ble_rid] fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* ---- child ---- */
        if (has_obs) {
            if (want_sudo) {
                if (use_nonint) {
                    execlp("sudo", "sudo", "-n", bridge_bin,
                           "--hci", hci_buf,
                           "--udp-port", udp_port,
                           "--obs-lat", obs_lat_str,
                           "--obs-lon", obs_lon_str,
                           (char *)NULL);
                } else {
                    execlp("sudo", "sudo", bridge_bin,
                           "--hci", hci_buf,
                           "--udp-port", udp_port,
                           "--obs-lat", obs_lat_str,
                           "--obs-lon", obs_lon_str,
                           (char *)NULL);
                }
            }
            execlp(bridge_bin, bridge_bin,
                   "--hci", hci_buf,
                   "--udp-port", udp_port,
                   "--obs-lat", obs_lat_str,
                   "--obs-lon", obs_lon_str,
                   (char *)NULL);
        } else {
            if (want_sudo) {
                if (use_nonint) {
                    execlp("sudo", "sudo", "-n", bridge_bin,
                           "--hci", hci_buf,
                           "--udp-port", udp_port,
                           (char *)NULL);
                } else {
                    execlp("sudo", "sudo", bridge_bin,
                           "--hci", hci_buf,
                           "--udp-port", udp_port,
                           (char *)NULL);
                }
            }
            execlp(bridge_bin, bridge_bin,
                   "--hci", hci_buf,
                   "--udp-port", udp_port,
                   (char *)NULL);
        }
        fprintf(stderr, "[ble_rid] exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    /* ---- parent: verify child didn't exit immediately (sudo -n failure) -- */
    usleep(200000);
    {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            if (want_sudo && use_nonint) {
                fprintf(stderr,
                    "[ble_rid] sudo -n launch failed; retrying with interactive sudo...\n");

                pid_t pid2 = fork();
                if (pid2 < 0) {
                    fprintf(stderr, "[ble_rid] fork failed (retry): %s\n", strerror(errno));
                    return -1;
                }
                if (pid2 == 0) {
                    if (has_obs) {
                        execlp("sudo", "sudo", bridge_bin,
                               "--hci", hci_buf,
                               "--udp-port", udp_port,
                               "--obs-lat", obs_lat_str,
                               "--obs-lon", obs_lon_str,
                               (char *)NULL);
                    } else {
                        execlp("sudo", "sudo", bridge_bin,
                               "--hci", hci_buf,
                               "--udp-port", udp_port,
                               (char *)NULL);
                    }
                    fprintf(stderr, "[ble_rid] exec failed (retry): %s\n", strerror(errno));
                    _exit(127);
                }

                usleep(200000);
                {
                    int st2 = 0;
                    pid_t rc2 = waitpid(pid2, &st2, WNOHANG);
                    if (rc2 == pid2) {
                        fprintf(stderr,
                            "[ble_rid] bridge launch failed after interactive retry (hci=%s).\n",
                            hci_buf);
                        return -1;
                    }
                }
                g_ble_pid = pid2;
                fprintf(stderr,
                    "[ble_rid] started bridge pid=%d hci=%s (sudo=on, noninteractive=off)\n",
                    (int)pid2, hci_buf);
                return 0;
            }

            fprintf(stderr,
                "[ble_rid] bridge launch failed (hci=%s). "
                "Hint: run 'sudo -v' before ./gnss-sim, or set "
                "BDS_BLE_RID_SUDO_NONINTERACTIVE=0 for interactive sudo prompt.\n",
                hci_buf);
            return -1;
        }
    }

    g_ble_pid = pid;
    fprintf(stderr,
        "[ble_rid] started bridge pid=%d hci=%s (sudo=%s, noninteractive=%s)\n",
        (int)pid, hci_buf,
        want_sudo ? "on" : "off",
        use_nonint ? "on" : "off");
    return 0;
}

void ble_rid_stop(void)
{
    if (g_ble_pid <= 0) return;
    pid_t pid = g_ble_pid;
    g_ble_pid = -1;

    if (!child_alive(pid)) return;

    kill(pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid) return;
        usleep(50000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void ble_rid_stop_fast(void)
{
    if (g_ble_pid <= 0) return;
    kill(g_ble_pid, SIGKILL);
    g_ble_pid = -1;
    int rc = system("pkill -KILL -f '[b]le-rid-bridge' >/dev/null 2>&1 || true");
    (void)rc;
}
