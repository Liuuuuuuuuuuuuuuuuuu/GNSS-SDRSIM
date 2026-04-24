#ifndef BLE_RID_RX_H
#define BLE_RID_RX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start BLE RID bridge using BDS_BLE_RID_HCI env var (default hci0).
 * Returns 0 on success / no-op, negative on error. */
int  ble_rid_start_from_env(void);

/* Stop BLE RID bridge process if running. */
void ble_rid_stop(void);

/* Fast stop for immediate app exit: sends SIGKILL without wait loops. */
void ble_rid_stop_fast(void);

/* Returns 1 if bridge process is running, else 0. */
int  ble_rid_is_active(void);

/* Write observer position to /tmp/bds_sim_obs.json (shared with Wi-Fi bridge).
 * Call this whenever the receiver position is updated.
 * NOTE: wifi_rid_set_observer_pos() writes the same file; calling either one
 *       is sufficient — both bridges read from the same path. */
void ble_rid_set_observer_pos(double lat_deg, double lon_deg);

#ifdef __cplusplus
}
#endif

#endif /* BLE_RID_RX_H */
