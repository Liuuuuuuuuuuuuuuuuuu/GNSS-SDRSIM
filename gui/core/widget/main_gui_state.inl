#ifndef MAIN_GUI_STATE_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
enum { PATH_SEG_QUEUED = 0, PATH_SEG_EXECUTING = 1 };

static constexpr int kMaxQueuedSegments = 64;

struct GuiPathSegment {
  double start_lat_deg;
  double start_lon_deg;
  double end_lat_deg;
  double end_lon_deg;
  int mode;
  int state;
  int chunk_total = 1;
  int chunk_done = 0;
  std::vector<LonLat> polyline;
  char path_file[256];
};

std::mutex g_path_seg_mtx;
std::vector<GuiPathSegment> g_path_segments;

std::atomic<bool> g_running(false);
std::atomic<bool> g_tx_active(false);
std::thread g_gui_thread;
QApplication *g_app = nullptr;
std::atomic<int> g_gui_screen_index{0};
static class MapWidget *g_active_widget = nullptr;

double g_start_bdt = 0.0;
std::chrono::steady_clock::time_point g_start_tp;
std::chrono::steady_clock::time_point g_tx_start_tp;
std::mutex g_time_mtx;

std::mutex g_ctrl_mtx;
GuiControlState g_ctrl = {
  .tx_gain = 50.0,
  .gain = 1.0,
  .fs_mhz = FS_OUTPUT_HZ / 1e6,
  .target_cn0 = CN0_TARGET_DBHZ,
  .selected_h_m = 0.0,
  .path_vmax_kmh = 72.0,
  .path_accel_mps2 = 2.0,
  .seed = 1,
  .single_prn = 0,
  .sat_mode = 2,
  .interference_selection = 0,
  .max_ch = 16,
  .single_candidates = {0},
  .single_candidate_count = 0,
  .single_candidate_idx = 0,
  .meo_only = false,
  .signal_mode = SIG_MODE_BDS,
  .byte_output = true,
  .iono_on = true,
  .usrp_external_clk = true,
  .interference_mode = false,
  .spoof_allowed = false,
  .running_ui = false,
  .llh_ready = false,
  .crossbow_direction_confirmed = false,
  .crossbow_distance_ok = false,
  .crossbow_dji_detected = false,
  .crossbow_dji_confidence = 0.0,
  .crossbow_auto_jam_enabled = false,
  .crossbow_unlocked = false,
  .show_detailed_ctrl = false,
  .hover_lb_panel = false,
  .hover_rb_panel = false,
  .panel_expand_progress = {0.0, 0.0},
  .rinex_name_bds = "N/A",
  .rinex_name_gps = "N/A",
  .n_gps_sats = 0,
  .n_bds_sats = 0
};

std::atomic<uint32_t> g_gui_start_req(0);
std::atomic<uint32_t> g_gui_launch_req(0);
std::atomic<uint32_t> g_gui_stop_req(0);
std::atomic<uint32_t> g_gui_exit_req(0);
std::atomic<uint32_t> g_gui_reset_waterfall_req(0);
std::atomic<uint32_t> g_gui_llh_pick_req(0);
std::atomic<uint32_t> g_gui_wifi_rid_allow_apply_req(0);
std::atomic<uint32_t> g_gui_wifi_rid_block_apply_req(0);
std::atomic<uint32_t> g_gui_wifi_rid_mode_apply_req(0);
std::atomic<uint32_t> g_gui_crossbow_unlock_req(0);
std::mutex g_llh_pick_mtx;
double g_llh_pick_lat_deg = 0.0;
double g_llh_pick_lon_deg = 0.0;
double g_llh_pick_h_m = 0.0;
std::mutex g_gui_wifi_rid_allow_mtx;
std::string g_gui_wifi_rid_allow_csv;
std::mutex g_gui_wifi_rid_applied_mtx;
std::string g_gui_wifi_rid_applied_csv;
bool g_gui_wifi_rid_applied_initialized = false;
std::mutex g_gui_wifi_rid_block_mtx;
std::string g_gui_wifi_rid_block_csv;
std::mutex g_gui_wifi_rid_block_applied_mtx;
std::string g_gui_wifi_rid_block_applied_csv;
bool g_gui_wifi_rid_block_applied_initialized = false;
std::mutex g_gui_wifi_rid_mode_mtx;
bool g_gui_wifi_rid_mode_mixed_enabled = true;
std::mutex g_gui_wifi_rid_mode_applied_mtx;
bool g_gui_wifi_rid_mode_mixed_applied = true;
bool g_gui_wifi_rid_mode_applied_initialized = false;

std::mutex g_gui_alert_mtx;
std::string g_gui_alert_text;
int g_gui_alert_level = 0; // 1=warn, 2=error, others=info
std::chrono::steady_clock::time_point g_gui_alert_expire_tp =
  std::chrono::steady_clock::time_point::min();

struct WaterfallState {
  QImage image;
  int width;
  int height;
};

static bool handle_control_click(int x, int y, int win_width, int win_height) {
  return control_logic_handle_click(
      x, y, win_width, win_height, &g_ctrl, &g_ctrl_mtx, &g_gui_start_req,
      &g_gui_stop_req, &g_gui_exit_req, &g_runtime_abort);
}

static bool control_value_text_for_field(int field_id, char *out,
                                         size_t out_sz) {
  return control_logic_value_text_for_field(field_id, out, out_sz, &g_ctrl,
                                            &g_ctrl_mtx);
}

static bool handle_control_value_input(int field_id, const char *input) {
  return control_logic_handle_value_input(field_id, input, &g_ctrl,
                                          &g_ctrl_mtx);
}

static bool handle_control_slider_drag(int slider_id, int x, int win_width,
                                       int win_height) {
  return control_logic_handle_slider_drag(slider_id, x, win_width, win_height,
                                          &g_ctrl, &g_ctrl_mtx);
}
#endif // MAIN_GUI_STATE_INL_CONTEXT
