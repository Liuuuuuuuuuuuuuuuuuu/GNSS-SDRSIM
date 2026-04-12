#ifndef WIFI_RID_RX_H
#define WIFI_RID_RX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start Wi-Fi RID bridge if BDS_WIFI_RID_IFACE is set. Returns 0 on success/no-op. */
int wifi_rid_start_from_env(void);

/* Stop Wi-Fi RID bridge process if running. */
void wifi_rid_stop(void);

/* Returns 1 if bridge process is running, else 0. */
int wifi_rid_is_active(void);

/* Write observer (receiver) GPS position to /tmp/bds_sim_obs.json so the
 * Wi-Fi RID Python bridge can compute bearing and distance to drone targets.
 * Call this whenever the receiver position is updated. */
void wifi_rid_set_observer_pos(double lat_deg, double lon_deg);

#ifdef __cplusplus
}
#endif

#endif
