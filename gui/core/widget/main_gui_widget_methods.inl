#ifndef MAIN_GUI_WIDGET_METHODS_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
void MapWidget::update_receiver_animation(std::chrono::steady_clock::time_point now,
                                          bool running_ui, bool *moved) {
  if (moved)
    *moved = false;

  const bool raw_valid = (g_receiver_valid != 0);
  if (!raw_valid) {
    receiver_anim_valid_ = false;
    receiver_anim_last_tp_ = now;
    return;
  }

  const double raw_lat = g_receiver_lat_deg;
  const double raw_lon = g_receiver_lon_deg;

  auto wrap_lon = [](double lon) {
    while (lon < -180.0)
      lon += 360.0;
    while (lon >= 180.0)
      lon -= 360.0;
    return lon;
  };
  auto lon_delta = [&](double to_lon, double from_lon) {
    return wrap_lon(to_lon - from_lon);
  };

  if (!receiver_anim_valid_) {
    receiver_anim_valid_ = true;
    receiver_anim_lat_deg_ = raw_lat;
    receiver_anim_lon_deg_ = wrap_lon(raw_lon);
    receiver_anim_target_lat_deg_ = receiver_anim_lat_deg_;
    receiver_anim_target_lon_deg_ = receiver_anim_lon_deg_;
    receiver_anim_last_tp_ = now;
    if (moved)
      *moved = true;
    return;
  }

  const double target_lat_diff = std::fabs(raw_lat - receiver_anim_target_lat_deg_);
  const double target_lon_diff = std::fabs(lon_delta(raw_lon, receiver_anim_target_lon_deg_));
  if (target_lat_diff > 1e-9 || target_lon_diff > 1e-9) {
    receiver_anim_target_lat_deg_ = raw_lat;
    receiver_anim_target_lon_deg_ = wrap_lon(raw_lon);
  }

  double dt_sec =
      std::chrono::duration<double>(now - receiver_anim_last_tp_).count();
  if (dt_sec < 0.0)
    dt_sec = 0.0;
  if (dt_sec > 0.20)
    dt_sec = 0.20;
  receiver_anim_last_tp_ = now;

  const double prev_lat = receiver_anim_lat_deg_;
  const double prev_lon = receiver_anim_lon_deg_;

  if (!running_ui) {
    receiver_anim_lat_deg_ = receiver_anim_target_lat_deg_;
    receiver_anim_lon_deg_ = receiver_anim_target_lon_deg_;
  } else if (dt_sec > 0.0) {
    const double tau_sec = 0.15;  /* Reduced from 0.30s for faster UI responsiveness */
    double alpha = 1.0 - std::exp(-dt_sec / tau_sec);
    alpha = clamp_double(alpha, 0.0, 1.0);

    receiver_anim_lat_deg_ +=
        (receiver_anim_target_lat_deg_ - receiver_anim_lat_deg_) * alpha;
    receiver_anim_lon_deg_ = wrap_lon(
        receiver_anim_lon_deg_ +
        lon_delta(receiver_anim_target_lon_deg_, receiver_anim_lon_deg_) * alpha);

    const double remain_lat =
        std::fabs(receiver_anim_target_lat_deg_ - receiver_anim_lat_deg_);
    const double remain_lon = std::fabs(
        lon_delta(receiver_anim_target_lon_deg_, receiver_anim_lon_deg_));
    if (remain_lat < 1e-8 && remain_lon < 1e-8) {
      receiver_anim_lat_deg_ = receiver_anim_target_lat_deg_;
      receiver_anim_lon_deg_ = receiver_anim_target_lon_deg_;
    }
  }

  if (moved) {
    const double dlat = std::fabs(receiver_anim_lat_deg_ - prev_lat);
    const double dlon = std::fabs(lon_delta(receiver_anim_lon_deg_, prev_lon));
    *moved = (dlat > 1e-8 || dlon > 1e-8);
  }
}

void MapWidget::onTick() {
  static bool last_running_ui = false;
  static bool last_tx_active = false;
  if (!g_running.load()) {
    close();
    return;
  }

  if (!map_ready_signal_emitted_) {
    const bool panel_ready = osm_panel_rect_.width() > 64 && osm_panel_rect_.height() > 64;
    const size_t cached_tiles = tile_cache_.size();
    if (panel_ready && cached_tiles >= (size_t)map_ready_min_tiles_) {
      map_ready_signal_emitted_ = true;
      if (!map_ready_signal_path_.empty()) {
        std::ofstream ofs(map_ready_signal_path_, std::ios::out | std::ios::trunc);
        if (ofs.good()) {
          ofs << "tiles=" << cached_tiles << " pending=" << tile_pending_.size() << "\n";
          ofs.close();
        }
      }
    }
  }

  if (user_geo_focus_pending_ && !user_geo_focus_applied_ && user_geo_valid_ &&
      !user_map_interacted_ && map_ready_signal_emitted_) {
    osm_zoom_ = user_geo_focus_zoom_;
    osm_zoom_base_ = user_geo_focus_zoom_;
    osm_center_px_x_ = osm_lon_to_world_x(user_geo_lon_deg_, osm_zoom_);
    osm_center_px_y_ = osm_lat_to_world_y(user_geo_lat_deg_, osm_zoom_);
    normalize_osm_center();
    request_visible_tiles();
    notify_nfz_viewport_changed();
    osm_bg_needs_redraw_ = true;
    update(osm_panel_rect_);
    user_geo_focus_pending_ = false;
    user_geo_focus_applied_ = true;
  }

  bool tutorial_state_changed = tutorial_sync_control_panel_page(
      tutorial_overlay_visible_, tutorial_step_, &g_ctrl.show_detailed_ctrl);
  update_overlay_widget_visibility();

  bool llh_ready = false;
  int signal_mode = SIG_MODE_BDS;
  bool interference_mode = false;
  bool running_ui = false;
  int interference_selection = -1;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    llh_ready = g_ctrl.llh_ready;
    signal_mode = (int)g_ctrl.signal_mode;
    interference_mode = g_ctrl.interference_mode;
    running_ui = g_ctrl.running_ui;
    interference_selection = g_ctrl.interference_selection;
  }

  // Crossbow backend policy:
  // 100~25m => spoof guidance, and only when target follows spoof direction
  // and enters 10~15m does backend signal path switch to JAM.
  // UI selection stays in crossbow mode.
  if (running_ui && dji_detect_mgr_) {
    double nearest_hostile_m = -1.0;
    double nearest_speed_mps = -1.0;
    bool nearest_has_speed = false;
    const auto targets = dji_detect_mgr_->targets_snapshot();
    for (const auto &t : targets) {
      if (!t.confirmed_dji || t.whitelisted) {
        continue;
      }
      double eval_distance_m = -1.0;
      if (t.has_geo && g_receiver_valid) {
        eval_distance_m = distance_m_approx(g_receiver_lat_deg, g_receiver_lon_deg,
                                            t.drone_lat, t.drone_lon);
      } else if (t.has_distance) {
        eval_distance_m = t.distance_m;
      }
      if (eval_distance_m < 0.0) {
        continue;
      }
      if (nearest_hostile_m < 0.0 || eval_distance_m < nearest_hostile_m) {
        nearest_hostile_m = eval_distance_m;
        nearest_speed_mps = t.speed_mps;
        nearest_has_speed = t.has_velocity;
      }
    }

    if (interference_selection == 2 && !crossbow_auto_jam_latched_) {
      if (nearest_hostile_m > 25.0 && nearest_hostile_m <= 100.0 &&
          !crossbow_spoof_zone_latched_) {
        crossbow_spoof_zone_latched_ = true;
        map_gui_push_alert(
            1,
            QString("Crossbow spoof engagement zone (target %.0fm)")
                .arg(nearest_hostile_m)
                .toUtf8()
                .constData());
      }
      if (nearest_hostile_m > 100.0 || nearest_hostile_m < 0.0) {
        crossbow_spoof_zone_latched_ = false;
      }

      const bool jam_ready = crossbow_spoof_following_;
      const bool in_jam_window = (nearest_hostile_m >= 10.0 && nearest_hostile_m <= 15.0);
      if (in_jam_window && jam_ready) {
        {
          std::lock_guard<std::mutex> lk(g_ctrl_mtx);
          if (g_ctrl.running_ui && g_ctrl.interference_selection == 2) {
            g_ctrl.crossbow_auto_jam_enabled = true;
            crossbow_auto_jam_latched_ = true;
            interference_mode = true;
          }
        }
        map_gui_push_alert(
            2,
            QString("Crossbow backend signal switched to JAM (target %.0fm)")
                .arg(nearest_hostile_m)
                .toUtf8()
                .constData());
        update();
      }
    }

    // Status hint while jamming: near-zero speed at close range may indicate landing.
    if ((interference_selection == 1 || crossbow_auto_jam_latched_) &&
        nearest_hostile_m >= 0.0 && nearest_hostile_m <= 25.0 &&
        nearest_has_speed && nearest_speed_mps >= 0.0 && nearest_speed_mps < 1.2 &&
        !crossbow_landing_hint_alerted_) {
      crossbow_landing_hint_alerted_ = true;
      map_gui_push_alert(1, "Target speed very low in JAM zone: possible forced landing");
    }
  }

  // Reset one-shot latch when runtime stops.
  if (!running_ui && crossbow_auto_jam_latched_) {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.crossbow_auto_jam_enabled = false;
    crossbow_auto_jam_latched_ = false;
    crossbow_spoof_zone_latched_ = false;
    crossbow_landing_hint_alerted_ = false;
  }

  // Track crossbow START and reset spoof-following state.
  if (running_ui && interference_selection == 2 && crossbow_start_ms_ == 0) {
    crossbow_start_ms_ = QDateTime::currentMSecsSinceEpoch();
    crossbow_spoof_following_ = false;
    crossbow_spoof_following_alerted_ = false;
    crossbow_beam_bearing_deg_ = crossbow_direction_locked_ ? crossbow_locked_bearing_deg_ : 0.0;
  }
  if (!running_ui) {
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      g_ctrl.crossbow_auto_jam_enabled = false;
    }
    crossbow_start_ms_ = 0;
    crossbow_spoof_following_ = false;
    crossbow_spoof_following_alerted_ = false;
    crossbow_spoof_zone_latched_ = false;
    crossbow_landing_hint_alerted_ = false;
  }

  // Spoof-following detection:
  // After 5s of crossbow spoofing, check if any confirmed hostile target's
  // estimated heading points in the spoof-beam direction (±60°).
  const qint64 follow_grace_ms = 5000;
  if (running_ui && interference_selection == 2 && dji_detect_mgr_ &&
      crossbow_start_ms_ > 0 && crossbow_direction_locked_ &&
      !crossbow_spoof_following_) {
    const qint64 now_chk = QDateTime::currentMSecsSinceEpoch();
    if (now_chk - crossbow_start_ms_ >= follow_grace_ms) {
      const auto targets_chk = dji_detect_mgr_->targets_snapshot();
      for (const auto &t : targets_chk) {
        if (!t.confirmed_dji || t.whitelisted || !t.has_velocity) continue;
        double ang_diff = t.heading_deg - crossbow_beam_bearing_deg_;
        while (ang_diff >  180.0) ang_diff -= 360.0;
        while (ang_diff < -180.0) ang_diff += 360.0;
        if (std::fabs(ang_diff) <= 60.0) {
          crossbow_spoof_following_ = true;
          break;
        }
      }
      if (crossbow_spoof_following_ && !crossbow_spoof_following_alerted_) {
        crossbow_spoof_following_alerted_ = true;
        map_gui_push_alert(
            1,
            "Crossbow: target confirmed following spoof direction — JAM ready");
      }
    }
  }

  if (running_ui && tutorial_overlay_visible_) {
    tutorial_overlay_visible_ = false;
    tutorial_enabled_ = false;
    update_overlay_widget_visibility();
  }
  if (!llh_ready) {
    has_selected_llh_ = false;
    has_preview_segment_ = false;
    preview_confirm_in_progress_ = false;
    preview_polyline_.clear();
    plan_status_.clear();
  }

  if (running_ui != last_running_ui) {
    last_running_ui = running_ui;
    if (!running_ui) {
      g_gui_reset_waterfall_req.fetch_add(1);
    }
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
  bool receiver_moved = false;
  update_receiver_animation(now, running_ui, &receiver_moved);

    const bool smart_route_loading_anim =
      running_ui && has_preview_segment_ &&
      preview_mode_ == PATH_MODE_PLAN && !preview_plan_route_ready_;

  if (running_ui && receiver_anim_valid_ && receiver_moved &&
      now >= next_receiver_draw_tick_) {
    do {
      next_receiver_draw_tick_ += std::chrono::milliseconds(16);
    } while (next_receiver_draw_tick_ <= now);
    update(osm_rect);
    update(map_rect);
  }

  if (smart_route_loading_anim && now >= next_receiver_draw_tick_) {
    do {
      next_receiver_draw_tick_ += std::chrono::milliseconds(16);
    } while (next_receiver_draw_tick_ <= now);
    update(osm_rect);
  }

  const bool tx_active_now = g_tx_active.load();
  const bool crossbow_staging_panel =
      running_ui && (interference_selection == 2) && !tx_active_now;
  if (!running_ui) {
    last_tx_active = false;
  } else if (tx_active_now != last_tx_active) {
    last_tx_active = tx_active_now;
    update(osm_rect);
  }

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
    const QString sys_name =
        (signal_mode == SIG_MODE_GPS)
            ? tr_text("scene.sys_gps")
            : ((signal_mode == SIG_MODE_MIXED) ? tr_text("scene.sys_mixed")
                                               : tr_text("scene.sys_bds"));
    if (interference_mode)
      std::snprintf(buf, sizeof(buf), "%s",
                    tr_text("scene.jam_fmt").arg(sys_name).toUtf8().constData());
    else if (used_mixed_fallback)
      std::snprintf(buf, sizeof(buf), "%s",
                    tr_text("scene.sat_fallback_fmt")
                        .arg(sys_name)
                        .arg((int)sats_.size())
                        .toUtf8()
                        .constData());
    else
      std::snprintf(buf, sizeof(buf), "%s",
                    tr_text("scene.sat_fmt")
                        .arg(sys_name)
                        .arg((int)sats_.size())
                        .toUtf8()
                        .constData());
    stat_text_ = buf;
    // Update visible sat counts in g_ctrl so ch slider limit stays current
    {
      int ng = 0, nb = 0;
      for (const auto &sp : sats_) { if (sp.is_gps) ++ng; else ++nb; }
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      g_ctrl.n_gps_sats = ng;
      g_ctrl.n_bds_sats = nb;
      // Clamp max_ch to new limit in case mode/sats changed
      int lim_n = (g_ctrl.signal_mode == SIG_MODE_GPS)  ? ng :
                  (g_ctrl.signal_mode == SIG_MODE_BDS)  ? nb :
                                                           ng + nb;
      int lim = (lim_n > 0) ? lim_n : 16;
      if (g_ctrl.max_ch > lim) g_ctrl.max_ch = lim;
    }
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
    if (tutorial_overlay_visible_) {
      // Full-window refresh avoids clip-region artifacts when tutorial card moves.
      update();
    }

    if (scene_dirty) {
      update(map_rect);
    }

    if (crossbow_staging_panel) {
      if (time_dirty || scene_dirty || spec_draw_tick || waterfall_dirty) {
        update(bottom_right_rect);
      }
    } else if (running_ui) {
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

  update_alert_overlay();
  refresh_tick_timer();
}

void MapWidget::draw_map_panel(QPainter &p, const QRect &map_rect) {
  if (map_rect.width() <= 0 || map_rect.height() <= 0)
    return;
  const bool receiver_valid = receiver_anim_valid_ || (g_receiver_valid != 0);
  const double receiver_lat = receiver_anim_valid_ ? receiver_anim_lat_deg_
                                                   : g_receiver_lat_deg;
  const double receiver_lon = receiver_anim_valid_ ? receiver_anim_lon_deg_
                                                   : g_receiver_lon_deg;

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

  GuiControlState st;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    st = g_ctrl;
  }

  p.drawImage(map_rect.topLeft(), map_panel_static_bg_);

  QColor color_border("#c6d4e6");

    const bool show_crossbow_radar =
      st.running_ui && (st.interference_selection == 2);
  if (show_crossbow_radar) {
    const QRect radar_rect = map_rect.adjusted(8, 8, -8, -8);
    const QPoint c = radar_rect.center();
    const int r = std::max(36, std::min(radar_rect.width(), radar_rect.height()) / 2 - 16);

    QLinearGradient panel_grad(radar_rect.topLeft(), radar_rect.bottomLeft());
    panel_grad.setColorAt(0.0, QColor("#0c1d2f"));
    panel_grad.setColorAt(1.0, QColor("#071321"));
    p.setPen(Qt::NoPen);
    p.setBrush(panel_grad);
    p.drawRoundedRect(radar_rect, 10, 10);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(64, 168, 94, 125), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    p.drawEllipse(c, (int)std::llround(r * 0.66), (int)std::llround(r * 0.66));
    p.drawEllipse(c, (int)std::llround(r * 0.33), (int)std::llround(r * 0.33));
    p.drawLine(c.x() - r, c.y(), c.x() + r, c.y());
    p.drawLine(c.x(), c.y() - r, c.x(), c.y() + r);

    const double max_range_m = 300.0;
    const double ring_marks[] = {25.0, 50.0, 100.0, 200.0, 300.0};
    for (double m : ring_marks) {
      const double rr = (m / max_range_m) * r;
      const int rp = (int)std::llround(std::max(3.0, rr));
      p.setPen(QPen(QColor(46, 204, 113, m >= 100.0 ? 140 : 110), 1,
                    m == 25.0 ? Qt::DashLine : Qt::SolidLine));
      p.drawEllipse(c, rp, rp);
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    if (dji_detect_mgr_) {
      const auto snap = dji_detect_mgr_->targets_snapshot();
      auto bearing_from_to_deg = [](double lat0, double lon0, double lat1, double lon1) {
        const double to_rad = M_PI / 180.0;
        const double to_deg = 180.0 / M_PI;
        const double phi1 = lat0 * to_rad;
        const double phi2 = lat1 * to_rad;
        const double dlon = (lon1 - lon0) * to_rad;
        const double y = std::sin(dlon) * std::cos(phi2);
        const double x = std::cos(phi1) * std::sin(phi2) -
                         std::sin(phi1) * std::cos(phi2) * std::cos(dlon);
        double brg = std::atan2(y, x) * to_deg;
        if (brg < 0.0) brg += 360.0;
        return brg;
      };

      for (const auto &t : snap) {
        double used_bearing = t.bearing_deg;
        double used_distance = t.distance_m;
        bool used_has_bearing = t.has_bearing;
        bool used_has_distance = t.has_distance;
        if (t.has_geo && receiver_valid) {
          used_distance = distance_m_approx(receiver_lat, receiver_lon,
                                            t.drone_lat, t.drone_lon);
          used_bearing = bearing_from_to_deg(receiver_lat, receiver_lon,
                                             t.drone_lat, t.drone_lon);
          used_has_bearing = std::isfinite(used_bearing);
          used_has_distance = std::isfinite(used_distance) && used_distance > 0.0;
        }

        const long long now_epoch_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const long long age_ms_raw = now_epoch_ms - (long long)t.last_seen_ms;
        const long long age_ms = std::max(0LL, std::min(2000LL, age_ms_raw));
        if (t.has_velocity && used_has_bearing && used_has_distance && age_ms > 0) {
          const double age_s = (double)age_ms * 0.001;
          const double speed_mps = std::max(0.0, std::min(80.0, t.speed_mps));
          const double hdg_rad = t.heading_deg * M_PI / 180.0;
          const double brg_rad = used_bearing * M_PI / 180.0;

          // Convert polar target to local NE frame, forward-predict by speed*age.
          double n = used_distance * std::cos(brg_rad);
          double e = used_distance * std::sin(brg_rad);
          n += std::cos(hdg_rad) * speed_mps * age_s;
          e += std::sin(hdg_rad) * speed_mps * age_s;

          used_distance = std::sqrt(n * n + e * e);
          used_bearing = std::atan2(e, n) * 180.0 / M_PI;
          if (used_bearing < 0.0) used_bearing += 360.0;
          used_has_bearing = std::isfinite(used_bearing);
          used_has_distance = std::isfinite(used_distance) && used_distance > 0.0;
        }

        if (t.whitelisted || !used_has_bearing || !used_has_distance || used_distance <= 0.0) {
          continue;
        }
        const bool drawable_target =
            (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
             t.source == QStringLiteral("bt-le-rid") ||
             t.source == QStringLiteral("ble-rid") ||
             t.source == QStringLiteral("wifi-nan-rid") ||
             t.source == QStringLiteral("wifi-rid"));
        if (!drawable_target) {
          continue;
        }

        const double norm = std::max(0.0, std::min(1.0, used_distance / max_range_m));
        const double rr = std::max(6.0, (1.0 - norm) * r);
        const double ang = (used_bearing - 90.0) * M_PI / 180.0;
        const int x = c.x() + (int)std::llround(std::cos(ang) * rr);
        const int y = c.y() + (int)std::llround(std::sin(ang) * rr);

        const QColor point_color =
            (t.confirmed_dji || t.source == QStringLiteral("aoa-passive"))
                ? QColor(255, 85, 85, 230)
                : ((t.source == QStringLiteral("wifi-rid") ||
                    t.source == QStringLiteral("wifi-nan-rid"))
                       ? QColor(132, 204, 22, 220)
                       : QColor(56, 189, 248, 230));
        p.setPen(Qt::NoPen);
        p.setBrush(point_color);
        p.drawEllipse(QPoint(x, y), 4, 4);

        if (t.confirmed_dji || t.source == QStringLiteral("wifi-rid") ||
            t.source == QStringLiteral("wifi-nan-rid") ||
            t.source == QStringLiteral("bt-le-rid") ||
            t.source == QStringLiteral("ble-rid")) {
          p.setPen(QColor(196, 255, 223, 210));
          QFont old_target_font = p.font();
          QFont target_font = old_target_font;
          target_font.setPointSize(std::max(8, old_target_font.pointSize() - 1));
          p.setFont(target_font);
          const QString tag = QStringLiteral("%1m %2ms")
                                  .arg((int)std::llround(used_distance))
                                  .arg((int)age_ms);
          p.drawText(x + 6, y - 6, tag);
          p.setFont(old_target_font);
        }
      }
    }

    p.setPen(QColor("#8ab4f8"));
    QFont old_font = p.font();
    QFont title_font = old_font;
    title_font.setPointSize(std::max(10, old_font.pointSize() + 1));
    title_font.setBold(true);
    p.setFont(title_font);
    p.drawText(QRect(radar_rect.x() + 10, radar_rect.y() + 8,
                     radar_rect.width() - 20, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
           QStringLiteral("Radar"));
    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setPen(QPen(color_border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(map_rect.adjusted(0, 0, -1, -1), 10, 10);
    return;
  }

  map_draw_satellite_layer(p, map_rect, sats_, st, g_active_prn_mask, MAX_SAT,
                           receiver_valid, receiver_lat,
                           receiver_lon, ui_language_);

  p.setPen(QPen(color_border, 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(map_rect.adjusted(0, 0, -1, -1), 10, 10);
}
#endif // MAIN_GUI_WIDGET_METHODS_INL_CONTEXT
