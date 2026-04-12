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
    const double tau_sec = 0.30;
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
      if (!t.confirmed_dji || t.whitelisted || !t.has_distance || t.distance_m < 0.0) {
        continue;
      }
      if (nearest_hostile_m < 0.0 || t.distance_m < nearest_hostile_m) {
        nearest_hostile_m = t.distance_m;
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
  bool receiver_moved = false;
  update_receiver_animation(now, running_ui, &receiver_moved);

  if (running_ui && receiver_anim_valid_ && receiver_moved &&
      now >= next_receiver_draw_tick_) {
    do {
      next_receiver_draw_tick_ += std::chrono::milliseconds(33);
    } while (next_receiver_draw_tick_ <= now);
    update(osm_rect);
    update(map_rect);
  }

  const bool tx_active_now = g_tx_active.load();
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

  const bool crossbow_running = st.running_ui && (st.interference_selection == 2);
  const bool tx_active = g_tx_active.load();
  if (!crossbow_running) {
    p.drawImage(map_rect.topLeft(), map_panel_static_bg_);
  }

  QColor color_border("#c6d4e6");

  if (crossbow_running) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient radar_bg(map_rect.topLeft(), map_rect.bottomRight());
    radar_bg.setColorAt(0.0, QColor("#06111d"));
    radar_bg.setColorAt(0.55, QColor("#081823"));
    radar_bg.setColorAt(1.0, QColor("#041019"));
    p.fillRect(map_rect, radar_bg);

    QColor radar_grid(64, 168, 94, 120);
    QColor radar_border(125, 211, 252, 180);
    QColor radar_text("#dbeafe");
    QColor radar_dim("#8ab4f8");

    const QRect radar_rect = map_rect.adjusted(14, 24, -14, -14);
    const QPoint c = radar_rect.center();
    const int r = std::max(28, std::min(radar_rect.width(), radar_rect.height()) / 2 - 4);

    p.setPen(QPen(radar_grid, 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    p.drawEllipse(c, (int)std::llround(r * 0.66), (int)std::llround(r * 0.66));
    p.drawEllipse(c, (int)std::llround(r * 0.33), (int)std::llround(r * 0.33));
    p.drawLine(c.x() - r, c.y(), c.x() + r, c.y());
    p.drawLine(c.x(), c.y() - r, c.x(), c.y() + r);

    const double max_range_m = 100.0;
    const double ring_marks[] = {10.0, 25.0, 50.0, 100.0};
    for (double m : ring_marks) {
      const double rr = (m / max_range_m) * r;
      const int rp = (int)std::llround(std::max(3.0, rr));
      p.setPen(QPen(QColor(46, 204, 113, m >= 100.0 ? 140 : 110), 1,
                    m == 25.0 ? Qt::DashLine : Qt::SolidLine));
      p.drawEllipse(c, rp, rp);
      p.setPen(QColor(132, 229, 166, 185));
      p.drawText(c.x() + rp + 4, c.y() - 2, QString("%1m").arg((int)m));
    }

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const double sweep_rad = (double)(now_ms % 4000) / 4000.0 * 2.0 * M_PI;
    for (int i = 0; i < 12; ++i) {
      const double a = sweep_rad - (double)i * (M_PI / 32.0);
      const int alpha = std::max(15, 130 - i * 10);
      p.setPen(QPen(QColor(40, 255, 120, alpha), i == 0 ? 2.2 : 1.2));
      const int ex = c.x() + (int)std::llround(std::cos(a) * r);
      const int ey = c.y() + (int)std::llround(std::sin(a) * r);
      p.drawLine(c, QPoint(ex, ey));
    }

    p.setPen(radar_text);
    QFont old_font = p.font();
    QFont title_font = old_font;
    title_font.setBold(true);
    p.setFont(title_font);
    p.drawText(QRect(map_rect.x() + 14, map_rect.y() + 8,
                     map_rect.width() - 28, 20),
               Qt::AlignLeft | Qt::AlignVCenter, "Crossbow Radar");

    // Flow strip: makes START/LAUNCH/TX phase explicit without guessing.
    auto draw_chip = [&](const QRect &rc, const QString &txt, const QColor &fill,
                         const QColor &stroke) {
      p.setPen(QPen(stroke, 1));
      p.setBrush(fill);
      p.drawRoundedRect(rc, 9, 9);
      p.setPen(QColor("#e5e7eb"));
      p.drawText(rc.adjusted(8, 0, -8, 0), Qt::AlignCenter, txt);
    };

    const int chip_h = 22;
    int chip_x = map_rect.x() + 14;
    const int chip_y = map_rect.y() + 30;
    draw_chip(QRect(chip_x, chip_y, 88, chip_h),
              QString("DETECT ON"),
              QColor(15, 118, 110, 180), QColor(45, 212, 191, 210));
    chip_x += 96;
    if (tx_active) {
      draw_chip(QRect(chip_x, chip_y, 84, chip_h),
                QString("TX LIVE"),
                QColor(185, 28, 28, 180), QColor(252, 165, 165, 220));
    } else {
      draw_chip(QRect(chip_x, chip_y, 116, chip_h),
                QString("WAIT LAUNCH"),
                QColor(30, 64, 175, 170), QColor(96, 165, 250, 220));
    }
          p.setPen(QColor("#94a3b8"));
          p.drawText(QRect(map_rect.x() + 14, map_rect.y() + 54, map_rect.width() - 28, 16),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 QString("Flow: START -> DETECT -> LAUNCH -> TX"));

    int hostile_count = 0;
    int wifi_count = 0;
    int nan_count = 0;
    int ble_count = 0;
    int dji_count = 0;
    if (dji_detect_mgr_) {
      const auto dots = dji_detect_mgr_->targets_snapshot();
      for (const auto &t : dots) {
        const bool drawable_target =
            ((!t.whitelisted) && t.has_bearing && t.has_distance && t.distance_m > 0.0 &&
             (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
              t.source == QStringLiteral("bt-le-rid") ||
              t.source == QStringLiteral("wifi-nan-rid") ||
              t.source == QStringLiteral("wifi-rid")));
        if (!drawable_target)
          continue;

        hostile_count++;
        if (t.source == QStringLiteral("wifi-rid")) {
          wifi_count++;
        } else if (t.source == QStringLiteral("wifi-nan-rid")) {
          nan_count++;
        } else if (t.source == QStringLiteral("bt-le-rid")) {
          ble_count++;
        } else if (t.confirmed_dji || t.source == QStringLiteral("aoa-passive")) {
          dji_count++;
        }
        const double norm = std::max(0.0, std::min(1.0, t.distance_m / max_range_m));
        const double rr = std::max(6.0, (1.0 - norm) * r);
        const double ang = (t.bearing_deg - 90.0) * M_PI / 180.0;
        const int x = c.x() + (int)std::llround(std::cos(ang) * rr);
        const int y = c.y() + (int)std::llround(std::sin(ang) * rr);

        p.setPen(Qt::NoPen);
        const QColor target_color =
            t.confirmed_dji ? QColor(255, 85, 85, 230)
            : (t.source == QStringLiteral("aoa-passive")
                   ? QColor(251, 191, 36, 230)
               : (t.source == QStringLiteral("wifi-rid")
                 ? QColor(132, 204, 22, 230)
                 : (t.source == QStringLiteral("wifi-nan-rid")
                    ? QColor(20, 184, 166, 230)
                    : QColor(56, 189, 248, 230))));
        p.setBrush(target_color);
        p.drawEllipse(QPoint(x, y), 5, 5);

        if (t.has_velocity && t.speed_mps > 0.2) {
          const double tail = std::min(18.0, 2.0 + t.speed_mps * 1.2);
          const int tx = x - (int)std::llround(std::cos(ang) * tail);
          const int ty = y - (int)std::llround(std::sin(ang) * tail);
          p.setPen(QPen(target_color.lighter(115), 1.1));
          p.drawLine(QPoint(x, y), QPoint(tx, ty));
        }
      }
    }

    // Source legend improves tactical interpretation speed.
    const int legend_y = map_rect.bottom() - 52;
    int legend_x = map_rect.x() + 16;
    auto draw_legend_item = [&](const QColor &col, const QString &txt) {
      p.setPen(Qt::NoPen);
      p.setBrush(col);
      p.drawEllipse(QPoint(legend_x + 5, legend_y + 6), 4, 4);
      p.setPen(QColor("#cbd5e1"));
      p.drawText(QRect(legend_x + 12, legend_y - 2, 120, 16),
                 Qt::AlignLeft | Qt::AlignVCenter, txt);
      legend_x += 102;
    };
    draw_legend_item(QColor(255, 85, 85, 230), QString("DJI/AoA"));
    draw_legend_item(QColor(56, 189, 248, 230), QString("BLE RID"));
    draw_legend_item(QColor(132, 204, 22, 230), QString("Wi-Fi RID"));
    draw_legend_item(QColor(20, 184, 166, 230), QString("Wi-Fi NAN"));

    p.setFont(old_font);
    p.setPen(hostile_count > 0 ? QColor(252, 165, 165) : radar_dim);
    p.drawText(QRect(map_rect.x() + 14, map_rect.bottom() - 28,
                     map_rect.width() - 28, 18),
               Qt::AlignLeft | Qt::AlignVCenter,
               hostile_count > 0 ? QString("Targets: %1").arg(hostile_count)
                                 : QString("No target"));
    p.setPen(QColor("#93c5fd"));
    p.drawText(QRect(map_rect.x() + 14, map_rect.bottom() - 28,
                     map_rect.width() - 28, 18),
                 Qt::AlignRight | Qt::AlignVCenter,
                 QString("DJI:%1  BLE:%2  WiFi:%3  NAN:%4")
                   .arg(dji_count)
                   .arg(ble_count)
                   .arg(wifi_count)
                   .arg(nan_count));
    p.restore();
  } else {
    map_draw_satellite_layer(p, map_rect, sats_, st, g_active_prn_mask, MAX_SAT,
                             receiver_valid, receiver_lat,
                             receiver_lon, ui_language_);
  }

  p.setPen(QPen(color_border, 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(map_rect.adjusted(0, 0, -1, -1), 10, 10);
}
