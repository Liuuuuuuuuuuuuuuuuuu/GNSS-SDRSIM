#ifndef MAP_GUI_H
#define MAP_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "bdssim.h"

void start_map_gui(double start_bdt);
void update_map_gui_start(double start_bdt);
void stop_map_gui(void);
void map_gui_set_screen_index(int screen_index);
void map_gui_set_control_defaults(const sim_config_t *cfg);
void map_gui_set_mode_policy(int spoof_allowed);
void map_gui_get_control_config(sim_config_t *cfg, double *target_cn0);
void map_gui_set_single_prn_candidates(const int *prns, int count);
void map_gui_set_rinex_names(const char *rinex_path_bds, const char *rinex_path_gps);
void map_gui_reset_monitor_views(void);
void map_gui_mark_init_start(void);
void map_gui_set_run_state(int running);
void map_gui_set_tx_active(int active);
void map_gui_set_llh_ready(int ready);
void map_gui_set_dji_detect_status(int detected, double confidence);
void map_gui_set_dji_whitelist_csv(const char *csv);
void* map_gui_get_dji_detect_manager(void);
void map_gui_set_selected_llh(double lat_deg, double lon_deg, double h_m);
void map_gui_set_selected_llh_centered(double lat_deg, double lon_deg,
									   double h_m);void map_gui_set_location_auto_zoom(double lat_deg, double lon_deg, double h_m);int map_gui_get_preview_prn(void);
void map_gui_set_selected_altitude(double h_m);
int map_gui_consume_selected_llh(double *lat_deg, double *lon_deg, double *h_m);
int map_gui_consume_wifi_rid_allow_ids(char *csv, size_t csv_sz);
int map_gui_consume_wifi_rid_block_ids(char *csv, size_t csv_sz);
int map_gui_consume_wifi_rid_mixed_mode(int *enabled);
int map_gui_consume_crossbow_unlock_request(void);
int map_gui_get_default_spoof_llh(double *lat_deg, double *lon_deg, double *h_m);
int map_gui_consume_start_request(void);
int map_gui_consume_launch_request(void);
int map_gui_consume_stop_request(void);
int map_gui_consume_exit_request(void);
void map_gui_notify_path_segment_started(void);
void map_gui_notify_path_segment_finished(void);
void map_gui_notify_path_segment_undo(void);
void map_gui_clear_path_segments(void);
void map_gui_reset_interaction_state(void);
void map_gui_pump_events(void);
void map_gui_push_alert(int level, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* MAP_GUI_H */
