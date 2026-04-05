void MapWidget::onTick() {
  static bool last_running_ui = false;
  if (!g_running.load()) {
    close();
    return;
  }

  bool tutorial_state_changed = tutorial_sync_control_panel_page(
      tutorial_overlay_visible_, tutorial_step_, &g_ctrl.show_detailed_ctrl);
  update_overlay_widget_visibility();

  if (QApplication::mouseButtons() == Qt::NoButton) {
    QPoint hover_pos = mapFromGlobal(QCursor::pos());
    if (rect().contains(hover_pos)) {
      update_hover_help(hover_pos);
    } else {
      clear_hover_help();
    }
  }

  bool llh_ready = false;
  int signal_mode = SIG_MODE_BDS;
  bool interference_mode = false;
  bool running_ui = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    llh_ready = g_ctrl.llh_ready;
    signal_mode = (int)g_ctrl.signal_mode;
    interference_mode = g_ctrl.interference_mode;
    running_ui = g_ctrl.running_ui;
  }

  if (running_ui && tutorial_overlay_visible_) {
    tutorial_overlay_visible_ = false;
    tutorial_enabled_ = false;
    update_overlay_widget_visibility();
  }
  if (!llh_ready) {
    has_selected_llh_ = false;
    has_preview_segment_ = false;
    preview_polyline_.clear();
    plan_status_.clear();
  }

  if (running_ui != last_running_ui) {
    last_running_ui = running_ui;
    osm_bg_needs_redraw_ = true;
    update(QRect(0, 0, width() / 2, height()));
  }

  bool tutorial_waveform_preview =
      tutorial_overlay_visible_ && !running_ui &&
      (tutorial_step_ >= 9 && tutorial_step_ <= 12);

  bool redraw = false;
  auto now = std::chrono::steady_clock::now();
  const int win_width = width();
  const int win_height = height();
  const QRect osm_rect(0, 0, win_width / 2, win_height);
  const QRect map_rect(win_width / 2, 0, win_width - win_width / 2,
                       win_height / 2);
  const QRect bottom_right_rect(win_width / 2, win_height / 2,
                                win_width - win_width / 2,
                                win_height - win_height / 2);
  if (!map_panel_bootstrap_redraw_done_ && !map_rect.isEmpty()) {
    update(map_rect);
    map_panel_bootstrap_redraw_done_ = true;
  }

  bool time_dirty = false;
  if (now >= next_time_tick_) {
    do {
      next_time_tick_ += std::chrono::milliseconds(100);
    } while (next_time_tick_ <= now);
    build_time_info(&time_info_);
    redraw = true;
    time_dirty = true;
  }

  bool scene_dirty = false;
  if (now >= next_scene_tick_) {
    do {
      next_scene_tick_ += std::chrono::seconds(1);
    } while (next_scene_tick_ <= now);
    compute_sat_points(sats_, time_info_.bdt_week, time_info_.bdt_sow,
                       signal_mode);
    bool used_mixed_fallback = false;
    if (sats_.empty() && signal_mode != SIG_MODE_MIXED) {
      compute_sat_points(sats_, time_info_.bdt_week, time_info_.bdt_sow,
                         SIG_MODE_MIXED);
      used_mixed_fallback = !sats_.empty();
    }
    char buf[64];
    const char *sys_name =
        (signal_mode == SIG_MODE_GPS)
            ? "GPS"
            : ((signal_mode == SIG_MODE_MIXED) ? "BDS+GPS" : "Non-GEO BDS");
    if (interference_mode)
      std::snprintf(buf, sizeof(buf), "JAM %s (Pseudo-Legit PRN)", sys_name);
    else if (used_mixed_fallback)
      std::snprintf(buf, sizeof(buf), "%s Satellites: %zu (fallback mixed)",
                    sys_name, sats_.size());
    else
      std::snprintf(buf, sizeof(buf), "%s Satellites: %zu", sys_name,
                    sats_.size());
    stat_text_ = buf;
    redraw = true;
    scene_dirty = true;
  }

  bool spec_draw_tick = false;
  bool waterfall_dirty = false;
  pthread_mutex_lock(&g_gui_spectrum_mtx);
  uint64_t seq = g_gui_spectrum_seq;
  pthread_mutex_unlock(&g_gui_spectrum_mtx);
  if (seq != last_spec_seq_) {
    last_spec_seq_ = seq;
    spec_draw_tick = true;
    fetch_spectrum_snapshot(&spec_snap_);
    redraw = true;
  }

  if (g_gui_reset_waterfall_req.exchange(0) != 0) {
    wf_.image = QImage();
    wf_.width = 0;
    wf_.height = 0;
    waterfall_dirty = true;
    redraw = true;
  }

  if (spec_draw_tick) {
    update_waterfall_image();
    waterfall_dirty = true;
  }

  if (tutorial_state_changed) {
    redraw = true;
    update();
  } else {
    if (scene_dirty) {
      update(map_rect);
    }

    if (running_ui) {
      static long long last_runtime_sec = -1;
      long long runtime_sec = 0;
      {
        std::lock_guard<std::mutex> lk(g_time_mtx);
        const auto &base_tp = g_tx_active.load() ? g_tx_start_tp : g_start_tp;
        runtime_sec = std::chrono::duration_cast<std::chrono::seconds>(
                          now - base_tp)
                          .count();
        if (runtime_sec < 0)
          runtime_sec = 0;
      }
      if (runtime_sec != last_runtime_sec) {
        last_runtime_sec = runtime_sec;
        update(osm_runtime_rect_);
      }
    } else if (time_dirty) {
      update(bottom_right_rect);
    }

    if (waterfall_dirty) {
      if (running_ui || tutorial_waveform_preview) {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, false);
        update(QRect(panel_x, panel_y, panel_w, panel_h));
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, true);
        update(QRect(panel_x, panel_y, panel_w, panel_h));
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, false);
        update(QRect(panel_x, panel_y, panel_w, panel_h));
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, true);
        update(QRect(panel_x, panel_y, panel_w, panel_h));
      }
    }

    if (redraw && !scene_dirty && !time_dirty && !spec_draw_tick &&
        !waterfall_dirty) {
      update();
    }
  }

  refresh_tick_timer();
}

void MapWidget::draw_map_panel(QPainter &p, const QRect &map_rect) {
  if (map_rect.width() <= 0 || map_rect.height() <= 0)
    return;

  if (map_panel_static_bg_.isNull() || map_panel_static_bg_size_ != map_rect.size()) {
    map_panel_static_bg_size_ = map_rect.size();
    map_panel_static_bg_ = QImage(map_panel_static_bg_size_, QImage::Format_RGB32);
    map_panel_static_bg_.fill(QColor("#0b1f36"));

    QPainter cache_p(&map_panel_static_bg_);
    cache_p.setRenderHint(QPainter::Antialiasing, false);

    QColor color_ocean_top("#15395d");
    QColor color_ocean_bottom("#0b1f36");
    QColor color_grid("#5f7ea0");
    QColor color_land("#8ac07a");

    QLinearGradient map_grad(QPointF(0.0, 0.0), QPointF(0.0, (double)map_rect.height()));
    map_grad.setColorAt(0.0, color_ocean_top);
    map_grad.setColorAt(1.0, color_ocean_bottom);
    cache_p.fillRect(QRect(QPoint(0, 0), map_rect.size()), map_grad);

    cache_p.setPen(QPen(color_grid, 1));
    for (int lon = -180; lon <= 180; lon += 30) {
      int x = lon_to_x((double)lon, map_rect.width());
      cache_p.drawLine(x, 0, x, map_rect.height() - 1);
    }
    for (int lat = -60; lat <= 60; lat += 30) {
      int y = lat_to_y((double)lat, map_rect.height());
      cache_p.drawLine(0, y, map_rect.width() - 1, y);
    }

    cache_p.setPen(QPen(color_land, 1));
    cache_p.setBrush(Qt::NoBrush);
    if (shp_ok_) {
      map_draw_shp_land(cache_p, shp_parts_, QRect(QPoint(0, 0), map_rect.size()));
    } else {
      map_draw_fallback_land(cache_p, QRect(QPoint(0, 0), map_rect.size()));
    }

    cache_p.setPen(QColor("#f7fbff"));
    map_draw_ticks(cache_p, QRect(QPoint(0, 0), map_rect.size()));
  }

  p.drawImage(map_rect.topLeft(), map_panel_static_bg_);

  QColor color_border("#c6d4e6");

  GuiControlState st;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    st = g_ctrl;
  }

  map_draw_satellite_layer(p, map_rect, sats_, st, g_active_prn_mask, MAX_SAT,
                           g_receiver_valid, g_receiver_lat_deg,
                           g_receiver_lon_deg);

  p.setPen(QPen(color_border, 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(map_rect.adjusted(0, 0, -1, -1), 10, 10);
}
