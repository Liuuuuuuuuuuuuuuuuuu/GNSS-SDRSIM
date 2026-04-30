#ifndef MAIN_GUI_C_API_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
extern "C" void start_map_gui(double start_bdt) {
  if (g_running.load())
    return;

  {
    std::lock_guard<std::mutex> lk(g_time_mtx);
    g_start_bdt = start_bdt;
    g_start_tp = std::chrono::steady_clock::now();
    g_tx_start_tp = g_start_tp;
  }

  g_gui_start_req.store(0);
  g_gui_launch_req.store(0);
  g_gui_stop_req.store(0);
  g_gui_exit_req.store(0);
  map_gui_reset_monitor_views();
  map_gui_set_run_state(0);
  g_running.store(true);
  g_gui_thread = std::thread(gui_thread_main);
}

extern "C" void map_gui_set_screen_index(int screen_index) {
  if (screen_index < 0)
    screen_index = 0;
  g_gui_screen_index.store(screen_index);
}

extern "C" void update_map_gui_start(double start_bdt) {
  if (!g_running.load())
    return;
  std::lock_guard<std::mutex> lk(g_time_mtx);
  g_start_bdt = start_bdt;
}

extern "C" void stop_map_gui(void) {
  if (!g_running.load() && !g_gui_thread.joinable())
    return;

  g_running.store(false);
  QApplication *app = g_app;
  if (app) {
    QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
  }

  if (g_gui_thread.joinable()) {
    g_gui_thread.join();
  }
}

extern "C" void map_gui_set_control_defaults(const sim_config_t *cfg) {
  if (!cfg)
    return;
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.tx_gain = clamp_double(cfg->tx_gain, 0.0, 100.0);
  g_ctrl.gain = clamp_double(cfg->gain, 0.1, 20.0);
  g_ctrl.signal_mode = (cfg->signal_mode <= SIG_MODE_MIXED)
                           ? cfg->signal_mode
                           : (cfg->signal_gps ? SIG_MODE_GPS : SIG_MODE_BDS);
  g_ctrl.fs_mhz = snap_fs_to_mode_grid_hz(cfg->fs, g_ctrl.signal_mode) / 1e6;
  g_ctrl.target_cn0 = clamp_double(g_target_cn0, 20.0, 60.0);
  g_ctrl.selected_h_m = cfg->llh[2];
  g_ctrl.max_ch = clamp_int(cfg->max_ch, 1, 16);
  g_ctrl.path_vmax_kmh = 72.0;
  g_ctrl.path_accel_mps2 = 2.0;
  g_ctrl.seed = 1;
  g_ctrl.sat_mode = (cfg->single_prn > 0) ? 0 : (cfg->prn37_only ? 1 : 2);
  g_ctrl.single_prn = cfg->single_prn;
  g_ctrl.single_candidate_count = 0;
  g_ctrl.single_candidate_idx = 0;
  g_ctrl.meo_only = cfg->meo_only;
  g_ctrl.byte_output = cfg->byte_output;
  g_ctrl.iono_on = cfg->iono_on;
  g_ctrl.usrp_external_clk = cfg->usrp_external_clk;
  g_ctrl.interference_mode = cfg->interference_mode;
  g_ctrl.interference_selection = clamp_int(cfg->interference_selection, -1, 2);
  g_ctrl.spoof_allowed = false;
  if (g_ctrl.single_prn < 0)
    g_ctrl.single_prn = 0;
  if (g_ctrl.single_prn > 63)
    g_ctrl.single_prn = 63;
  g_ctrl.running_ui = false;
  g_ctrl.llh_ready = false;
  g_ctrl.crossbow_auto_jam_enabled = false;
  g_ctrl.crossbow_unlocked = false;
  g_ctrl.show_detailed_ctrl = false;
  auto _set_rnx = [](char *dst, std::size_t dsz, const char *path) {
    if (path && path[0]) {
      const char *base = std::strrchr(path, '/');
      base = base ? (base + 1) : path;
      std::snprintf(dst, dsz, "%s", base);
    } else {
      std::snprintf(dst, dsz, "N/A");
    }
  };
  _set_rnx(g_ctrl.rinex_name_bds, sizeof(g_ctrl.rinex_name_bds), cfg->rinex_file_bds);
  _set_rnx(g_ctrl.rinex_name_gps, sizeof(g_ctrl.rinex_name_gps), cfg->rinex_file_gps);
}

extern "C" void map_gui_set_mode_policy(int spoof_allowed) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.spoof_allowed = spoof_allowed ? true : false;
  if (g_ctrl.interference_selection < -1 || g_ctrl.interference_selection > 2) {
    g_ctrl.interference_selection = g_ctrl.spoof_allowed ? 0 : 1;
  }
  g_ctrl.interference_mode = (g_ctrl.interference_selection == 1);
}

extern "C" void map_gui_set_rinex_names(const char *rinex_path_bds,
                                        const char *rinex_path_gps) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  auto set_name = [](char *dst, std::size_t dsz, const char *path) {
    if (!path || path[0] == '\0') { std::snprintf(dst, dsz, "N/A"); return; }
    const char *base = std::strrchr(path, '/');
    base = base ? (base + 1) : path;
    std::snprintf(dst, dsz, "%s", base);
  };
  set_name(g_ctrl.rinex_name_bds, sizeof(g_ctrl.rinex_name_bds), rinex_path_bds);
  set_name(g_ctrl.rinex_name_gps, sizeof(g_ctrl.rinex_name_gps), rinex_path_gps);
}

extern "C" void map_gui_get_control_config(sim_config_t *cfg,
                                             double *target_cn0) {
  if (!cfg)
    return;

  std::lock_guard<std::mutex> lk(g_ctrl_mtx);

  cfg->tx_gain = clamp_double(g_ctrl.tx_gain, 0.0, 100.0);
  cfg->gain = clamp_double(g_ctrl.gain, 0.1, 20.0);
  cfg->signal_mode = g_ctrl.signal_mode;
  cfg->signal_gps = (g_ctrl.signal_mode == SIG_MODE_GPS);
  cfg->fs = snap_fs_to_mode_grid_hz(g_ctrl.fs_mhz * 1e6, g_ctrl.signal_mode);
  cfg->seed = 1;

  if (g_ctrl.sat_mode == 0) {
    cfg->single_prn = g_ctrl.single_prn > 0 ? g_ctrl.single_prn : 0;
    cfg->prn37_only = false;
  } else if (g_ctrl.sat_mode == 1) {
    cfg->single_prn = 0;
    cfg->prn37_only = true;
  } else {
    cfg->single_prn = 0;
    cfg->prn37_only = false;
  }

  cfg->meo_only = g_ctrl.meo_only;
  cfg->byte_output = g_ctrl.byte_output;
  cfg->iono_on = g_ctrl.iono_on;
  cfg->usrp_external_clk = g_ctrl.usrp_external_clk;
  int effective_selection = g_ctrl.interference_selection;
  if (!g_ctrl.crossbow_unlocked && effective_selection == 2) {
    effective_selection = 0;
  }
  if (effective_selection == 2 && g_ctrl.crossbow_auto_jam_enabled) {
    effective_selection = 1;
  }
  cfg->interference_selection = effective_selection;
  cfg->interference_mode = (effective_selection == 1);
  cfg->max_ch = clamp_int(g_ctrl.max_ch, 1, 16);

  if (target_cn0) {
    *target_cn0 = clamp_double(g_ctrl.target_cn0, 20.0, 60.0);
  }
}

extern "C" void map_gui_set_single_prn_candidates(const int *prns, int count) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);

  int n = count;
  if (n < 0)
    n = 0;
  if (n > 64)
    n = 64;
  g_ctrl.single_candidate_count = n;

  for (int i = 0; i < n; ++i) {
    int v = prns ? prns[i] : 0;
    g_ctrl.single_candidates[i] = clamp_int(v, 1, 63);
  }

  if (n <= 0) {
    g_ctrl.single_prn = 0;
    g_ctrl.single_candidate_idx = 0;
    return;
  }

  int idx = -1;
  for (int i = 0; i < n; ++i) {
    if (g_ctrl.single_candidates[i] == g_ctrl.single_prn) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    idx = 0;
  g_ctrl.single_candidate_idx = idx;
  g_ctrl.single_prn = g_ctrl.single_candidates[g_ctrl.single_candidate_idx];
}

extern "C" void map_gui_set_run_state(int running) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.running_ui = running ? true : false;
  if (!running) {
    g_ctrl.crossbow_auto_jam_enabled = false;
    g_tx_active.store(false);
    g_gui_reset_waterfall_req.fetch_add(1);
  }
}

extern "C" void map_gui_reset_monitor_views(void) {
  pthread_mutex_lock(&g_gui_spectrum_mtx);
  g_gui_spectrum_valid = 0;
  g_gui_time_valid = 0;
  g_gui_spectrum_bins = GUI_SPECTRUM_BINS;
  g_gui_time_samples = GUI_TIME_MON_SAMPLES;
  for (int i = 0; i < GUI_SPECTRUM_BINS; ++i) {
    g_gui_spectrum_db[i] = 0.0f;
  }
  for (int i = 0; i < GUI_TIME_MON_SAMPLES; ++i) {
    g_gui_time_iq[2 * i] = 0;
    g_gui_time_iq[2 * i + 1] = 0;
  }
  g_gui_spectrum_seq += 1;
  pthread_mutex_unlock(&g_gui_spectrum_mtx);
  g_gui_reset_waterfall_req.fetch_add(1);
}

extern "C" void map_gui_mark_init_start(void) {
  std::lock_guard<std::mutex> tlk(g_time_mtx);
  g_start_tp = std::chrono::steady_clock::now();
  g_tx_start_tp = g_start_tp;
}

extern "C" void map_gui_set_tx_active(int active) {
  if (active) {
    std::lock_guard<std::mutex> tlk(g_time_mtx);
    g_tx_start_tp = std::chrono::steady_clock::now();
  }
  g_tx_active.store(active ? true : false);
}

extern "C" void map_gui_set_llh_ready(int ready) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.llh_ready = ready ? true : false;
  if (!g_ctrl.llh_ready) {
    g_ctrl.crossbow_direction_confirmed = false;
    g_ctrl.crossbow_distance_ok = false;
  }
}

extern "C" void map_gui_set_dji_detect_status(int detected, double confidence) {
  MapWidget *w = g_active_widget;
  if (w) {
    w->set_dji_detect_status_public(detected, confidence);
    return;
  }

  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.crossbow_dji_detected = (detected != 0);
  g_ctrl.crossbow_dji_confidence = confidence;
}

extern "C" void map_gui_set_dji_whitelist_csv(const char *csv) {
  MapWidget *w = g_active_widget;
  if (w) {
    w->set_dji_whitelist_csv_public(QString::fromUtf8(csv ? csv : ""));
  }
}

extern "C" void* map_gui_get_dji_detect_manager(void) {
  MapWidget *w = g_active_widget;
  if (!w)
    return nullptr;
  return w->get_dji_detect_manager_public();
}

extern "C" int map_gui_get_default_spoof_llh(double *lat_deg, double *lon_deg,
                                               double *h_m) {
  MapWidget *w = g_active_widget;
  if (!w)
    return 0;
  return w->get_default_spoof_llh(lat_deg, lon_deg, h_m) ? 1 : 0;
}

extern "C" void map_gui_set_selected_llh(double lat_deg, double lon_deg,
                                           double h_m) {
  MapWidget *w = g_active_widget;
  if (!w)
    return;
  w->set_selected_llh_direct_public(lat_deg, lon_deg, h_m);
}

extern "C" void map_gui_set_selected_llh_centered(double lat_deg,
                                                     double lon_deg,
                                                     double h_m) {
  MapWidget *w = g_active_widget;
  if (!w)
    return;
  // Force-center behavior is disabled globally to preserve user map drag control.
  w->set_selected_llh_direct_public(lat_deg, lon_deg, h_m);
}

extern "C" void map_gui_set_selected_altitude(double h_m) {
  MapWidget *w = g_active_widget;
  if (!w)
    return;
  w->set_selected_altitude_public(h_m);
}

extern "C" void map_gui_set_location_auto_zoom(double lat_deg, double lon_deg, double h_m) {
  MapWidget *w = g_active_widget;
  if (!w)
    return;
  /* Set location */
  w->set_selected_llh_direct_public(lat_deg, lon_deg, h_m);
  /* Auto-zoom for 50m visibility */
  w->auto_zoom_for_location_public(lat_deg, lon_deg);
}

extern "C" int map_gui_get_preview_prn(void) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  if (g_ctrl.single_prn < 1 || g_ctrl.single_prn > 63)
    return 0;
  return g_ctrl.single_prn;
}

extern "C" int map_gui_consume_selected_llh(double *lat_deg, double *lon_deg,
                                              double *h_m) {
  uint32_t n = g_gui_llh_pick_req.exchange(0);
  if (n == 0)
    return 0;

  std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
  if (lat_deg)
    *lat_deg = g_llh_pick_lat_deg;
  if (lon_deg)
    *lon_deg = g_llh_pick_lon_deg;
  if (h_m)
    *h_m = g_llh_pick_h_m;
  return 1;
}

extern "C" int map_gui_consume_wifi_rid_allow_ids(char *csv, size_t csv_sz) {
  uint32_t n = g_gui_wifi_rid_allow_apply_req.exchange(0);
  if (n == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lk(g_gui_wifi_rid_allow_mtx);
  if (csv && csv_sz > 0) {
    std::snprintf(csv, csv_sz, "%s", g_gui_wifi_rid_allow_csv.c_str());
  }
  return 1;
}

extern "C" int map_gui_consume_wifi_rid_block_ids(char *csv, size_t csv_sz) {
  uint32_t n = g_gui_wifi_rid_block_apply_req.exchange(0);
  if (n == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lk(g_gui_wifi_rid_block_mtx);
  if (csv && csv_sz > 0) {
    std::snprintf(csv, csv_sz, "%s", g_gui_wifi_rid_block_csv.c_str());
  }
  return 1;
}

extern "C" int map_gui_consume_wifi_rid_mixed_mode(int *enabled) {
  uint32_t n = g_gui_wifi_rid_mode_apply_req.exchange(0);
  if (n == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lk(g_gui_wifi_rid_mode_mtx);
  if (enabled) {
    *enabled = g_gui_wifi_rid_mode_mixed_enabled ? 1 : 0;
  }
  return 1;
}

extern "C" int map_gui_consume_crossbow_unlock_request(void) {
  uint32_t n = g_gui_crossbow_unlock_req.exchange(0);
  return n == 0 ? 0 : 1;
}

extern "C" int map_gui_consume_start_request(void) {
  uint32_t n = g_gui_start_req.exchange(0);
  if (n > 0) {
    map_gui_reset_monitor_views();
  }
  return n > 0 ? 1 : 0;
}

extern "C" int map_gui_consume_launch_request(void) {
  uint32_t n = g_gui_launch_req.exchange(0);
  if (n > 0) {
    map_gui_reset_monitor_views();
  }
  return n > 0 ? 1 : 0;
}

extern "C" int map_gui_consume_stop_request(void) {
  uint32_t n = g_gui_stop_req.exchange(0);
  return n > 0 ? 1 : 0;
}

extern "C" int map_gui_consume_exit_request(void) {
  uint32_t n = g_gui_exit_req.exchange(0);
  return n > 0 ? 1 : 0;
}

extern "C" void map_gui_notify_path_segment_started(void) {
  std::lock_guard<std::mutex> lk(g_path_seg_mtx);
  if (!g_path_segments.empty()) {
    g_path_segments.front().state = PATH_SEG_EXECUTING;
  }
}

extern "C" void map_gui_notify_path_segment_finished(void) {
  std::lock_guard<std::mutex> lk(g_path_seg_mtx);
  if (!g_path_segments.empty()) {
    GuiPathSegment &front = g_path_segments.front();
    front.chunk_done = std::max(0, front.chunk_done + 1);
    if (front.chunk_done >= std::max(1, front.chunk_total)) {
      g_path_segments.erase(g_path_segments.begin());
    }
  }
}

extern "C" void map_gui_notify_path_segment_undo(void) {
  std::lock_guard<std::mutex> lk(g_path_seg_mtx);
  if (!g_path_segments.empty() &&
      g_path_segments.back().state == PATH_SEG_QUEUED) {
    GuiPathSegment &back = g_path_segments.back();
    if (back.chunk_total > 1) {
      back.chunk_total -= 1;
    } else {
      g_path_segments.pop_back();
    }
  }
}

extern "C" void map_gui_clear_path_segments(void) {
  std::lock_guard<std::mutex> lk(g_path_seg_mtx);
  g_path_segments.clear();
}

extern "C" void map_gui_reset_interaction_state(void) {
  MapWidget *w = g_active_widget;
  if (!w)
    return;
  w->reset_interaction_state_public();
}

extern "C" void map_gui_pump_events(void) {
  QApplication *app = g_app;
  if (app)
    app->processEvents();
}

extern "C" void map_gui_push_alert(int level, const char *message) {
  if (!message || !message[0])
    return;

  if (level < 0)
    level = 0;
  if (level > 2)
    level = 2;

  std::string msg = message;
  while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
    msg.pop_back();
  }

  auto now = std::chrono::steady_clock::now();
  int ttl_ms = 4500;
  if (level == 1)
    ttl_ms = 6500;
  if (level == 2)
    ttl_ms = 9000;

  {
    std::lock_guard<std::mutex> lk(g_gui_alert_mtx);
    if (now <= g_gui_alert_expire_tp && g_gui_alert_level > level) {
      return;
    }

    if (g_gui_alert_text == msg && g_gui_alert_level == level &&
        now <= g_gui_alert_expire_tp) {
      g_gui_alert_expire_tp =
          now + std::chrono::milliseconds(std::max(2200, ttl_ms / 2));
      return;
    }
    g_gui_alert_text = msg;
    g_gui_alert_level = level;
    g_gui_alert_expire_tp = now + std::chrono::milliseconds(ttl_ms);
  }
}
#endif // MAIN_GUI_C_API_INL_CONTEXT
