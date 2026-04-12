static constexpr int kMaxRouteCacheEntries = 96;

bool MapWidget::panel_point_to_llh(const QPoint &pos, double *lat_deg,
                                   double *lon_deg) const {
  if (!osm_panel_rect_.contains(pos))
    return false;

  double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
  double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
  double local_x = (double)(pos.x() - osm_panel_rect_.x());
  double local_y = (double)(pos.y() - osm_panel_rect_.y());
  double world_x = left + local_x;
  double world_y = top + local_y;

  if (lat_deg)
    *lat_deg = osm_world_y_to_lat(world_y, osm_zoom_);
  if (lon_deg)
    *lon_deg = osm_world_x_to_lon(world_x, osm_zoom_);
  return true;
}

void MapWidget::normalize_osm_center() {
  if (osm_zoom_ < 0)
    osm_zoom_ = 0;

  const double world_size = 256.0 * (double)(1 << osm_zoom_);
  if (world_size <= 0.0)
    return;

  while (osm_center_px_x_ < 0.0)
    osm_center_px_x_ += world_size;
  while (osm_center_px_x_ >= world_size)
    osm_center_px_x_ -= world_size;

  const double max_world_y = world_size - 1.0;
  osm_center_px_y_ = clamp_double(osm_center_px_y_, 0.0, max_world_y);
}

void MapWidget::start_route_prefetch(double lat0, double lon0, double lat1,
                                     double lon1) {
  if (!route_net_)
    return;

  QString key = map_route_cache_key(lat0, lon0, lat1, lon1);
  if (route_prefetch_cache_.find(key) != route_prefetch_cache_.end())
    return;
  if (route_prefetch_pending_.find(key) != route_prefetch_pending_.end())
    return;

  QString url = map_route_osrm_url(lat0, lon0, lat1, lon1);

  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");

  QNetworkReply *reply = route_net_->get(req);
  reply->setProperty("route_key", key);
  route_prefetch_pending_.insert(key);
}

void MapWidget::onRouteReply(QNetworkReply *reply) {
  if (!reply)
    return;

  QString key = reply->property("route_key").toString();
  bool route_ready = false;
  if (!key.isEmpty()) {
    route_prefetch_pending_.erase(key);
    if (reply->error() == QNetworkReply::NoError) {
      std::vector<LonLat> polyline;
      if (map_route_parse_osrm_geojson(reply->readAll(), &polyline)) {
        route_prefetch_cache_[key] = std::move(polyline);
        route_prefetch_order_.push_back(key);
        map_route_trim_cache(route_prefetch_cache_, route_prefetch_order_,
                 kMaxRouteCacheEntries);
        route_ready = true;
      }
    }

    if (key == preview_route_key_) {
      if (route_ready) {
        auto it = route_prefetch_cache_.find(key);
        if (it != route_prefetch_cache_.end() && it->second.size() >= 2) {
          preview_polyline_ = it->second;
          has_preview_segment_ = true;
          plan_status_ = tr_text("path.road_ready").toStdString();
        }
      } else {
        has_preview_segment_ = false;
        plan_status_ = tr_text("path.road_failed").toStdString();
      }
      update(osm_panel_rect_);
    }
  }

  reply->deleteLater();
}

bool MapWidget::get_current_plan_anchor(double *lat_deg, double *lon_deg) {
  std::lock_guard<std::mutex> lk(g_path_seg_mtx);
  if (!g_path_segments.empty()) {
    const GuiPathSegment &last = g_path_segments.back();
    if (lat_deg)
      *lat_deg = last.end_lat_deg;
    if (lon_deg)
      *lon_deg = last.end_lon_deg;
    return true;
  }

  if (g_receiver_valid) {
    if (lat_deg)
      *lat_deg = g_receiver_lat_deg;
    if (lon_deg)
      *lon_deg = g_receiver_lon_deg;
    return true;
  }

  if (has_selected_llh_) {
    if (lat_deg)
      *lat_deg = selected_lat_deg_;
    if (lon_deg)
      *lon_deg = selected_lon_deg_;
    return true;
  }
  return false;
}

void MapWidget::set_preview_target(const QPoint &pos, int mode) {
  double lat = 0.0;
  double lon = 0.0;
  if (!panel_point_to_llh(pos, &lat, &lon))
    return;

  double slat = 0.0;
  double slon = 0.0;
  if (!get_current_plan_anchor(&slat, &slon)) {
    plan_status_ = tr_text("path.need_anchor").toStdString();
    return;
  }

  preview_start_lat_deg_ = slat;
  preview_start_lon_deg_ = slon;
  preview_end_lat_deg_ = lat;
  preview_end_lon_deg_ = lon;
  preview_mode_ = mode;
  preview_polyline_.clear();
  preview_route_key_.clear();

  if (distance_m_approx(slat, slon, lat, lon) <= 0.5) {
    has_preview_segment_ = false;
    plan_status_ = tr_text("path.too_close").toStdString();
    return;
  }

  if (mode == PATH_MODE_LINE) {
    preview_polyline_.push_back({wrap_lon_deg(slon), slat});
    preview_polyline_.push_back({wrap_lon_deg(lon), lat});
    has_preview_segment_ = true;
    plan_status_ = tr_text("path.line_ready").toStdString();
    start_route_prefetch(slat, slon, lat, lon);
    return;
  }

  QString key = map_route_cache_key(slat, slon, lat, lon);
  preview_route_key_ = key;
  auto it = route_prefetch_cache_.find(key);
  if (it != route_prefetch_cache_.end() && it->second.size() >= 2) {
    preview_polyline_ = it->second;
    has_preview_segment_ = true;
    plan_status_ = tr_text("path.road_ready_cached").toStdString();
    return;
  }

  start_route_prefetch(slat, slon, lat, lon);
  has_preview_segment_ = false;
  plan_status_ = tr_text("path.road_loading").toStdString();
}

void MapWidget::confirm_preview_segment() {
  if (!has_preview_segment_) {
    plan_status_ = tr_text("path.no_preview").toStdString();
    return;
  }

  {
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    if ((int)g_path_segments.size() >= kMaxQueuedSegments) {
      plan_status_ = tr_text("path.queue_full").toStdString();
      return;
    }
  }

  char file_path[256] = {0};
  std::vector<LonLat> seg_polyline;
  double vmax_mps = 20.0;
  double accel_mps2 = 2.0;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    vmax_mps = g_ctrl.path_vmax_kmh / 3.6;
    accel_mps2 = g_ctrl.path_accel_mps2;
  }
  if (!build_segment_path_file(preview_start_lat_deg_, preview_start_lon_deg_,
                               preview_end_lat_deg_, preview_end_lon_deg_,
                               preview_mode_, vmax_mps, accel_mps2,
                               &preview_polyline_, file_path, &seg_polyline)) {
    plan_status_ = tr_text("path.build_failed").toStdString();
    return;
  }

  if (!enqueue_path_file_name(file_path)) {
    plan_status_ = tr_text("path.queue_reject").toStdString();
    return;
  }

  GuiPathSegment seg{};
  seg.start_lat_deg = preview_start_lat_deg_;
  seg.start_lon_deg = preview_start_lon_deg_;
  seg.end_lat_deg = preview_end_lat_deg_;
  seg.end_lon_deg = preview_end_lon_deg_;
  seg.mode = preview_mode_;
  seg.state = PATH_SEG_QUEUED;
  seg.polyline = std::move(seg_polyline);
  std::snprintf(seg.path_file, sizeof(seg.path_file), "%s", file_path);

  {
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    g_path_segments.push_back(seg);
  }

  has_preview_segment_ = false;
  preview_polyline_.clear();
  plan_status_ = tr_text("path.segment_queued").toStdString();
}

void MapWidget::try_undo_last_segment() {
  char removed[256] = {0};
  if (!delete_last_queued_path(removed, sizeof(removed))) {
    plan_status_ = tr_text("path.undo_fail").toStdString();
    return;
  }

  map_gui_notify_path_segment_undo();
  has_preview_segment_ = false;
  preview_polyline_.clear();
  plan_status_ = tr_text("path.undo_ok").toStdString();
}

bool MapWidget::lonlat_to_osm_screen(double lat_deg, double lon_deg,
                                     const QRect &panel, QPoint *out) const {
  if (!out)
    return false;
  double world = osm_world_size_for_zoom(osm_zoom_);
  double left = osm_center_px_x_ - (double)panel.width() * 0.5;
  double top = osm_center_px_y_ - (double)panel.height() * 0.5;
  double wx0 = osm_lon_to_world_x(lon_deg, osm_zoom_);
  double wy = osm_lat_to_world_y(lat_deg, osm_zoom_);

  int best_sx = 0;
  int best_sy = 0;
  double best_d2 = 1e30;
  bool ok = false;
  for (int k = -1; k <= 1; ++k) {
    double wx = wx0 + (double)k * world;
    int sx = panel.x() + (int)llround(wx - left);
    int sy = panel.y() + (int)llround(wy - top);
    double dx = (double)sx - (double)(panel.x() + panel.width() / 2);
    double dy = (double)sy - (double)(panel.y() + panel.height() / 2);
    double d2 = dx * dx + dy * dy;
    if (d2 < best_d2) {
      best_d2 = d2;
      best_sx = sx;
      best_sy = sy;
      ok = true;
    }
  }
  if (ok)
    *out = QPoint(best_sx, best_sy);
  return ok;
}

void MapWidget::draw_osm_panel(QPainter &p, const QRect &panel) {
  osm_panel_rect_ = panel;
  if (panel.width() < 8 || panel.height() < 8) return;
  request_visible_tiles();
  bool running_ui = false;
  bool jam_selected = false;
  int interference_selection = -1;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    interference_selection = g_ctrl.interference_selection;
    jam_selected = (interference_selection == 1);
  }

  const bool should_lock_crossbow_center =
      running_ui && (interference_selection == 2) && has_selected_llh_;
  if (!should_lock_crossbow_center) {
    crossbow_center_locked_ = false;
  } else {
    if (!crossbow_center_locked_) {
      crossbow_center_locked_ = true;
      crossbow_center_lock_lat_deg_ = selected_lat_deg_;
      crossbow_center_lock_lon_deg_ = selected_lon_deg_;
    }
    osm_center_px_x_ = osm_lon_to_world_x(crossbow_center_lock_lon_deg_, osm_zoom_);
    osm_center_px_y_ = osm_lat_to_world_y(crossbow_center_lock_lat_deg_, osm_zoom_);
    normalize_osm_center();
  }

  // Mode transition hook: switching from spoof/jam to cross should immediately
  // activate cross-specific interaction state.
  if (interference_selection != last_interference_selection_) {
    const bool entering_cross = (interference_selection == 2);
    last_interference_selection_ = interference_selection;
    if (entering_cross) {
      reset_crossbow_direction_state();
      sync_crossbow_ctrl_flags(false, false);
      if (has_selected_llh_) {
        sync_receiver_marker_to_selected_llh();
        QPoint selected_px;
        if (lonlat_to_osm_screen(selected_lat_deg_, selected_lon_deg_, panel,
                                 &selected_px)) {
          auto bearing_deg = [](double lat0_deg, double lon0_deg,
                                double lat1_deg, double lon1_deg) {
            const double lat0 = lat0_deg * M_PI / 180.0;
            const double lat1 = lat1_deg * M_PI / 180.0;
            const double dlon = (lon1_deg - lon0_deg) * M_PI / 180.0;
            const double y = std::sin(dlon) * std::cos(lat1);
            const double x = std::cos(lat0) * std::sin(lat1) -
                             std::sin(lat0) * std::cos(lat1) * std::cos(dlon);
            double brg = std::atan2(y, x) * 180.0 / M_PI;
            while (brg < 0.0) brg += 360.0;
            while (brg >= 360.0) brg -= 360.0;
            return brg;
          };
          const double to_zero_bearing =
              bearing_deg(selected_lat_deg_, selected_lon_deg_, 0.0, 0.0);
          const double axis_rad = (to_zero_bearing - 90.0) * M_PI / 180.0;
          const int ray_len = std::max(panel.width(), panel.height());
          crossbow_mouse_pos_ = QPoint(
              selected_px.x() + (int)std::llround(std::cos(axis_rad) * ray_len),
              selected_px.y() + (int)std::llround(std::sin(axis_rad) * ray_len));
        } else {
          crossbow_mouse_pos_ = panel.center();
        }
        crossbow_has_mouse_ = true;

        // Auto-zoom to ensure the 50m ring is well visible on crossbow entry.
        {
          const double cos_lat_az = std::max(0.15,
              std::cos(selected_lat_deg_ * M_PI / 180.0));
          // Target: 50m ring occupies ~120 px radius on screen.
          const double az_target_mpp = 50.0 / 120.0;
          const double az_zoom_f = std::log2(
              (40075016.0 * cos_lat_az) / (256.0 * az_target_mpp));
          const int az_zoom = std::max(15, std::min(20, (int)std::llround(az_zoom_f)));
          if (osm_zoom_ < az_zoom) {
            osm_zoom_ = az_zoom;
            osm_center_px_x_ = osm_lon_to_world_x(selected_lon_deg_, osm_zoom_);
            osm_center_px_y_ = osm_lat_to_world_y(selected_lat_deg_, osm_zoom_);
            normalize_osm_center();
            request_visible_tiles();
          }
        }
      }
    }
  }

  bool can_undo = false;
  if (running_ui) {
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    can_undo = !g_path_segments.empty() &&
               g_path_segments.back().state == PATH_SEG_QUEUED;
  }

  const bool dji_on = dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled();
  const bool tx_active = g_tx_active.load();
  long long elapsed_sec = 0;
  if (running_ui) {
    std::lock_guard<std::mutex> lk(g_time_mtx);
    const auto &base_tp = tx_active ? g_tx_start_tp : g_start_tp;
    elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - base_tp)
                      .count();
    if (elapsed_sec < 0)
      elapsed_sec = 0;
  }

  std::vector<MapOsmPanelSegment> segs;
  {
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    segs.reserve(g_path_segments.size());
    for (const auto &seg : g_path_segments) {
      MapOsmPanelSegment copy;
      copy.start_lat_deg = seg.start_lat_deg;
      copy.start_lon_deg = seg.start_lon_deg;
      copy.end_lat_deg = seg.end_lat_deg;
      copy.end_lon_deg = seg.end_lon_deg;
      copy.mode = map_osm_panel_path_mode_from_int(seg.mode);
      copy.state = map_osm_panel_segment_state_from_int(seg.state);
      copy.polyline = seg.polyline;
      segs.push_back(std::move(copy));
    }
  }

  MapOsmPanelInput osm_in;
  std::vector<DjiNfzZone> visible_nfz_zones;
  if (dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled()) {
    const auto &all_nfz_zones = dji_nfz_mgr_->get_zones();
    visible_nfz_zones.reserve(all_nfz_zones.size());
    for (const auto &zone : all_nfz_zones) {
      const int layer_idx = nfz_zone_layer_index(zone);
      if (layer_idx >= 0 && layer_idx < (int)nfz_layer_visible_.size() &&
          nfz_layer_visible_[layer_idx]) {
        visible_nfz_zones.push_back(zone);
      }
    }
  }

  osm_in.panel = panel;
  osm_in.language = ui_language_;
  osm_in.osm_zoom = osm_zoom_;
  osm_in.osm_zoom_base = osm_zoom_base_;
  osm_in.osm_center_px_x = osm_center_px_x_;
  osm_in.osm_center_px_y = osm_center_px_y_;
  osm_in.tile_cache = &tile_cache_;
  osm_in.path_segments = &segs;
  osm_in.preview_polyline = &preview_polyline_;
  osm_in.nfz_zones = visible_nfz_zones.empty() ? nullptr : &visible_nfz_zones;
  osm_in.coord_to_screen = [this, &panel](double lat, double lon, QPoint *out) {
    return this->lonlat_to_osm_screen(lat, lon, panel, out);
  };
  osm_in.nfz_enabled = dji_on;
  osm_in.has_selected_llh = has_selected_llh_;
  osm_in.selected_lat_deg = selected_lat_deg_;
  osm_in.selected_lon_deg = selected_lon_deg_;
  osm_in.has_preview_segment = has_preview_segment_;
  osm_in.preview_mode = map_osm_panel_path_mode_from_int(preview_mode_);
  osm_in.preview_start_lat_deg = preview_start_lat_deg_;
  osm_in.preview_start_lon_deg = preview_start_lon_deg_;
  osm_in.preview_end_lat_deg = preview_end_lat_deg_;
  osm_in.preview_end_lon_deg = preview_end_lon_deg_;
    osm_in.receiver_valid = receiver_anim_valid_ || (g_receiver_valid != 0);
    osm_in.receiver_lat_deg =
      receiver_anim_valid_ ? receiver_anim_lat_deg_ : g_receiver_lat_deg;
    osm_in.receiver_lon_deg =
      receiver_anim_valid_ ? receiver_anim_lon_deg_ : g_receiver_lon_deg;
  osm_in.running_ui = running_ui;
  osm_in.jam_selected = jam_selected;
  osm_in.can_undo = can_undo;
  osm_in.dark_map_mode = dark_map_mode_;
  osm_in.tutorial_enabled = tutorial_enabled_;
  osm_in.tutorial_hovered = tutorial_toggle_hovered_;
  osm_in.tutorial_overlay_visible = tutorial_overlay_visible_;
  osm_in.tutorial_step = tutorial_step_;
  osm_in.show_search_return = show_search_return_;
  osm_in.search_box_rect = search_box_ ? search_box_->geometry() : QRect();
  osm_in.show_launch_button =
      running_ui && (interference_selection == 2) && !tx_active;
  osm_in.nfz_layer_visible = nfz_layer_visible_;
  osm_in.tx_active = tx_active;
  osm_in.elapsed_sec = elapsed_sec;
  osm_in.scale_ruler_enabled = scale_ruler_enabled_;
  osm_in.scale_ruler_has_start = scale_ruler_has_start_;
  osm_in.scale_ruler_start_lat_deg = scale_ruler_start_lat_deg_;
  osm_in.scale_ruler_start_lon_deg = scale_ruler_start_lon_deg_;
  osm_in.scale_ruler_has_end = scale_ruler_has_end_;
  osm_in.scale_ruler_end_lat_deg = scale_ruler_end_lat_deg_;
  osm_in.scale_ruler_end_lon_deg = scale_ruler_end_lon_deg_;
  osm_in.scale_ruler_end_fixed = scale_ruler_end_fixed_;
  osm_in.plan_status = QString::fromStdString(plan_status_);

  const bool bg_needs_rebuild = osm_bg_needs_redraw_ || !osm_bg_cache_valid_ ||
      osm_bg_cache_size_ != panel.size() || osm_bg_cache_zoom_ != osm_zoom_ ||
      osm_bg_cache_center_px_x_ != osm_center_px_x_ ||
      osm_bg_cache_center_px_y_ != osm_center_px_y_ ||
      osm_bg_cache_dark_map_mode_ != dark_map_mode_;
  if (bg_needs_rebuild) {
    osm_bg_cache_size_ = panel.size();
    osm_bg_cache_zoom_ = osm_zoom_;
    osm_bg_cache_center_px_x_ = osm_center_px_x_;
    osm_bg_cache_center_px_y_ = osm_center_px_y_;
    osm_bg_cache_dark_map_mode_ = dark_map_mode_;
    osm_bg_needs_redraw_ = false;
    osm_bg_cache_ = QImage(osm_bg_cache_size_, QImage::Format_RGB32);
    osm_bg_cache_.fill(QColor("#081425"));
    if (!osm_bg_cache_.isNull()) {
      QPainter cache_p(&osm_bg_cache_);
      map_draw_osm_panel_background(cache_p, osm_in);
    }
    osm_bg_cache_valid_ = true;
  }

  if (!osm_bg_cache_.isNull()) {
    p.drawImage(panel.topLeft(), osm_bg_cache_);
  }

  MapOsmPanelState osm_out;
  map_draw_osm_panel_overlay(p, osm_in, &osm_out);
  lang_btn_rect_ = osm_out.lang_btn_rect;
  back_btn_rect_ = osm_out.back_btn_rect;
  nfz_btn_rect_ = osm_out.nfz_btn_rect;
  dark_mode_btn_rect_ = osm_out.dark_mode_btn_rect;
  tutorial_toggle_rect_ = osm_out.tutorial_toggle_rect;
  search_return_btn_rect_ = osm_out.search_return_btn_rect;
  osm_stop_btn_rect_ = osm_out.osm_stop_btn_rect;
  osm_launch_btn_rect_ = osm_out.osm_launch_btn_rect;
  osm_runtime_rect_ = osm_out.osm_runtime_rect;
  osm_scale_bar_rect_ = osm_out.osm_scale_bar_rect;
  osm_status_badge_rects_ = osm_out.status_badge_rects;
  osm_nfz_legend_row_rects_ = osm_out.nfz_legend_row_rects;

  // DJI model HUD: only show in crossbow mode, near map center-top.
  if (is_crossbow_mode() && dji_detect_ui_detected_) {
    QString model_txt = dji_detect_ui_model_.trimmed();
    if (model_txt.isEmpty()) {
      model_txt = QStringLiteral("Unknown model");
    }
    model_txt = model_txt.toUpper();
    const int conf_pct = (int)std::llround(std::max(0.0, std::min(1.0, dji_detect_ui_confidence_)) * 100.0);
    const int n_targets = std::max(1, dji_detect_ui_target_count_);

    // Build extended HUD with trajectory and state lines.
    QString line1 = QString("DJI x%1 | TOP %2 (%3%)")
                        .arg(n_targets)
                        .arg(model_txt)
                        .arg(conf_pct);
    QString line2;
    {
      // Find best target velocity for display.
      double best_spd = -1.0;
      double best_hdg = 0.0;
      if (dji_detect_mgr_) {
        const auto snap = dji_detect_mgr_->targets_snapshot();
        for (const auto &t : snap) {
          if (t.confirmed_dji && !t.whitelisted && t.has_velocity) {
            if (best_spd < 0.0 || t.speed_mps > best_spd) {
              best_spd = t.speed_mps;
              best_hdg = t.heading_deg;
            }
          }
        }
      }
      if (best_spd >= 0.0) {
        line2 = QString("HDG %1°  SPD %2 m/s")
                    .arg((int)std::llround(best_hdg))
                    .arg(best_spd, 0, 'f', 1);
      }
      if (crossbow_spoof_following_) {
        const QString tag = crossbow_auto_jam_latched_
                                ? "  ▶ JAMMING"
                                : "  ✓ FOLLOWING SPOOF";
        line2 = line2.isEmpty() ? tag.trimmed() : line2 + tag;
      } else if (crossbow_auto_jam_latched_) {
        const QString tag = "  ▶ JAMMING";
        line2 = line2.isEmpty() ? tag.trimmed() : line2 + tag;
      }
    }

    const bool two_lines = !line2.isEmpty();
    QFont old_font = p.font();
    QFont hud_font = old_font;
    hud_font.setBold(true);
    hud_font.setPointSize(std::max(11, std::min(16, panel.height() / 40)));
    p.setFont(hud_font);

    const int pad_x = 14;
    const int pad_y = 8;
    const int line_gap = 4;
    const int text_w1 = p.fontMetrics().horizontalAdvance(line1);
    const int text_w2 = two_lines ? p.fontMetrics().horizontalAdvance(line2) : 0;
    const int text_h  = p.fontMetrics().height();
    const int box_w = std::max(text_w1, text_w2) + pad_x * 2;
    const int box_h = two_lines ? (text_h * 2 + line_gap + pad_y * 2) : (text_h + pad_y * 2);
    const int box_x = panel.x() + (panel.width() - box_w) / 2;
    const int box_y = panel.y() + std::max(18, panel.height() / 7);
    const QRect box(box_x, box_y, box_w, box_h);

    // Border colour: red when jamming, yellow when following, blue otherwise.
    QColor border_col = crossbow_auto_jam_latched_
                            ? QColor(239, 68, 68, 220)
                            : (crossbow_spoof_following_ ? QColor(251, 191, 36, 220)
                                                         : QColor(125, 211, 252, 220));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border_col, 1.2));
    p.setBrush(QColor(8, 24, 42, 228));
    p.drawRoundedRect(box, 10, 10);
    p.setPen(QColor("#e5eefc"));
    const QRect r1(box.x(), box.y() + pad_y, box.width(), text_h);
    p.drawText(r1, Qt::AlignCenter, line1);
    if (two_lines) {
      p.setPen(crossbow_spoof_following_
                   ? (crossbow_auto_jam_latched_ ? QColor(252, 165, 165) : QColor(253, 224, 71))
                   : QColor("#b3c8e8"));
      const QRect r2(box.x(), r1.bottom() + line_gap, box.width(), text_h);
      p.drawText(r2, Qt::AlignCenter, line2);
    }
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setFont(old_font);
  }

  // Compact DJI/RID debug overlay for field verification.
  if (is_crossbow_mode() && dji_detect_mgr_) {
    const auto snap = dji_detect_mgr_->targets_snapshot();
    const auto rid_diag = dji_detect_mgr_->last_rid_diag_snapshot();
    int hostile_count = 0;
    int aoa_anon_count = 0;
    double nearest_aoa_m = -1.0;
    for (const auto &t : snap) {
      if (t.confirmed_dji && !t.whitelisted) {
        hostile_count++;
      }
      if (t.source == QStringLiteral("aoa-passive") && t.has_distance && t.distance_m > 0.0) {
        aoa_anon_count++;
        if (nearest_aoa_m < 0.0 || t.distance_m < nearest_aoa_m) {
          nearest_aoa_m = t.distance_m;
        }
      }
    }

    const int conf_pct = (int)std::llround(
        std::max(0.0, std::min(1.0, dji_detect_ui_confidence_)) * 100.0);
    const QString src_txt = dji_detect_ui_source_.trimmed().isEmpty()
                                ? QStringLiteral("n/a")
                                : dji_detect_ui_source_.trimmed();
    const QString aoa_dist_txt = (nearest_aoa_m > 0.0)
                                     ? QString::number(nearest_aoa_m, 'f', 1)
                                     : QStringLiteral("n/a");
    const QString dbg1 = QString("RID DBG  det=%1  conf=%2%%  src=%3")
                             .arg(dji_detect_ui_detected_ ? QStringLiteral("Y")
                                                           : QStringLiteral("N"))
                             .arg(conf_pct)
                             .arg(src_txt);
    const QString dbg2 = QString("targets=%1 hostile=%2 aoaAnon=%3 nearAoA=%4m")
                             .arg((int)snap.size())
                             .arg(hostile_count)
                             .arg(aoa_anon_count)
                             .arg(aoa_dist_txt);
    const QString dbg3 = QString("spoofFollow=%1 autoJam=%2")
                             .arg(crossbow_spoof_following_ ? QStringLiteral("Y")
                                                             : QStringLiteral("N"))
                             .arg(crossbow_auto_jam_latched_ ? QStringLiteral("Y")
                                                              : QStringLiteral("N"));

    const QString last_rid_line =
      rid_diag.has_sample
        ? QString("Last RID: %1 | %2")
            .arg(rid_diag.protocol.isEmpty() ? QStringLiteral("Unknown")
                             : rid_diag.protocol)
            .arg(rid_diag.has_distance
                 ? QString("%1m").arg(rid_diag.distance_m, 0, 'f', 1)
                 : QStringLiteral("dist n/a"))
        : QStringLiteral("Last RID: n/a");
    const QString geom_line = QString("Geom Valid: %1")
                    .arg(rid_diag.geom_valid ? QStringLiteral("YES")
                                 : QStringLiteral("NO"));
    const QString gate_line = QString("Gate Status: %1")
                    .arg(rid_diag.has_sample && !rid_diag.gate_status.isEmpty()
                         ? rid_diag.gate_status
                         : QStringLiteral("n/a"));

    QFont old_font = p.font();
    QFont dbg_font = old_font;
    dbg_font.setPointSize(std::max(8, std::min(11, panel.height() / 70)));
    dbg_font.setBold(true);
    p.setFont(dbg_font);

    const int pad_x = 10;
    const int pad_y = 6;
    const int line_gap = 2;
    const int w = std::max({
        p.fontMetrics().horizontalAdvance(dbg1),
        p.fontMetrics().horizontalAdvance(dbg2),
        p.fontMetrics().horizontalAdvance(dbg3)}) + pad_x * 2;
    const int h_line = p.fontMetrics().height();
    const int h = h_line * 3 + line_gap * 2 + pad_y * 2;
    const QRect box(panel.x() + 14, panel.y() + 56, w, h);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(56, 189, 248, 180), 1.0));
    p.setBrush(QColor(6, 18, 32, 220));
    p.drawRoundedRect(box, 8, 8);
    p.setPen(QColor(191, 219, 254, 235));
    p.drawText(QRect(box.x() + pad_x, box.y() + pad_y, box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, dbg1);
    p.setPen(QColor(147, 197, 253, 225));
    p.drawText(QRect(box.x() + pad_x, box.y() + pad_y + h_line + line_gap,
                     box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, dbg2);
    p.setPen(crossbow_auto_jam_latched_ ? QColor(252, 165, 165)
                                        : QColor(196, 181, 253));
    p.drawText(QRect(box.x() + pad_x,
                     box.y() + pad_y + (h_line + line_gap) * 2,
                     box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, dbg3);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int diag_w = std::max({
        p.fontMetrics().horizontalAdvance(last_rid_line),
        p.fontMetrics().horizontalAdvance(geom_line),
        p.fontMetrics().horizontalAdvance(gate_line)}) + pad_x * 2;
    const int diag_h = h_line * 3 + line_gap * 2 + pad_y * 2;
    const QRect diag_box(panel.right() - diag_w - 14, panel.y() + 56, diag_w, diag_h);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(45, 212, 191, 170), 1.0));
    p.setBrush(QColor(5, 22, 20, 218));
    p.drawRoundedRect(diag_box, 8, 8);
    p.setPen(QColor(204, 251, 241, 235));
    p.drawText(QRect(diag_box.x() + pad_x, diag_box.y() + pad_y,
                     diag_box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, last_rid_line);
    p.setPen(rid_diag.geom_valid ? QColor(110, 231, 183) : QColor(253, 186, 116));
    p.drawText(QRect(diag_box.x() + pad_x,
                     diag_box.y() + pad_y + h_line + line_gap,
                     diag_box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, geom_line);
    QColor gate_col = QColor(191, 219, 254, 225);
    if (rid_diag.gate_status == QStringLiteral("PASSED")) {
      gate_col = QColor(110, 231, 183);
    } else if (rid_diag.gate_status == QStringLiteral("BLOCKED_WHITELIST")) {
      gate_col = QColor(196, 181, 253);
    } else if (rid_diag.gate_status == QStringLiteral("BLOCKED_CONFIDENCE")) {
      gate_col = QColor(252, 165, 165);
    }
    p.setPen(gate_col);
    p.drawText(QRect(diag_box.x() + pad_x,
                     diag_box.y() + pad_y + (h_line + line_gap) * 2,
                     diag_box.width() - pad_x * 2, h_line),
               Qt::AlignLeft | Qt::AlignVCenter, gate_line);
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setFont(old_font);
  }

  // ── Crossbow geometric overlay + reverse NFZ retrieval + dual-zoom UI ─────
  if (is_crossbow_mode()) {
    if (has_selected_llh_) {
      if (dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled() && !crossbow_far_fetch_done_) {
        const double lat_span = 0.45; // ~50 km latitude span
        const double cos_lat = std::max(0.2, std::cos(selected_lat_deg_ * M_PI / 180.0));
        const double lon_span = lat_span / cos_lat;
        dji_nfz_mgr_->trigger_fetch(selected_lon_deg_ - lon_span,
                                    selected_lon_deg_ + lon_span,
                                    selected_lat_deg_ + lat_span,
                                    selected_lat_deg_ - lat_span,
                                    osm_zoom_);
        crossbow_far_fetch_done_ = true;
      }

      auto bearing_deg = [](double lat0_deg, double lon0_deg,
                            double lat1_deg, double lon1_deg) {
        const double lat0 = lat0_deg * M_PI / 180.0;
        const double lat1 = lat1_deg * M_PI / 180.0;
        const double dlon = (lon1_deg - lon0_deg) * M_PI / 180.0;
        const double y = std::sin(dlon) * std::cos(lat1);
        const double x = std::cos(lat0) * std::sin(lat1) -
                         std::sin(lat0) * std::cos(lat1) * std::cos(dlon);
        double brg = std::atan2(y, x) * 180.0 / M_PI;
        while (brg < 0.0) brg += 360.0;
        while (brg >= 360.0) brg -= 360.0;
        return brg;
      };

      bool has_forward_bearing = false;
      double forward_bearing_deg = 0.0;
      if (crossbow_direction_locked_) {
        forward_bearing_deg = crossbow_locked_bearing_deg_;
        has_forward_bearing = true;
      } else if (crossbow_has_mouse_ && !running_ui) {
        // During runtime (running_ui), freeze the bearing so the sector
        // direction stays stable — consistent with spoof mode behaviour.
        double mouse_lat = 0.0;
        double mouse_lon = 0.0;
        if (panel_point_to_llh(crossbow_mouse_pos_, &mouse_lat, &mouse_lon)) {
          forward_bearing_deg =
              bearing_deg(selected_lat_deg_, selected_lon_deg_, mouse_lat, mouse_lon);
          has_forward_bearing = true;
        }
      }
      if (!has_forward_bearing) {
        forward_bearing_deg = 0.0;
        has_forward_bearing = true;
      }

      double nfz_lat = 0.0;
      double nfz_lon = 0.0;
      double nfz_dist_m = 0.0;
      double nearest_reverse_dist_m = -1.0;
      bool has_valid_reverse_nfz = false;
      if (dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled() && has_forward_bearing) {
        const auto &all_zones = dji_nfz_mgr_->get_zones();
        has_valid_reverse_nfz = nfz_find_reverse_zone_center_in_range(
            all_zones,
            selected_lat_deg_, selected_lon_deg_,
            forward_bearing_deg,
          16000.0, 1.0e9,
            &nfz_lat, &nfz_lon,
            &nfz_dist_m,
            &nearest_reverse_dist_m);

        if (has_valid_reverse_nfz) {
          crossbow_cached_nfz_valid_ = true;
          crossbow_cached_nfz_bearing_deg_ = forward_bearing_deg;
          crossbow_cached_nfz_lat_deg_ = nfz_lat;
          crossbow_cached_nfz_lon_deg_ = nfz_lon;
          crossbow_cached_nfz_dist_m_ = nfz_dist_m;
        } else if (crossbow_cached_nfz_valid_ && crossbow_direction_locked_) {
          // Cache fallback only when direction is locked (right-click):
          // prevents convergent lines from flickering on zoom-induced data gaps.
          nfz_lat = crossbow_cached_nfz_lat_deg_;
          nfz_lon = crossbow_cached_nfz_lon_deg_;
          nfz_dist_m = distance_m_approx(selected_lat_deg_, selected_lon_deg_,
                                         nfz_lat, nfz_lon);
          has_valid_reverse_nfz = (nfz_dist_m >= 16000.0);
          nearest_reverse_dist_m = nfz_dist_m;
        } else {
          // Direction not locked and no NFZ found → discard stale cache.
          crossbow_cached_nfz_valid_ = false;
        }
      }

      {
        std::lock_guard<std::mutex> lk(g_ctrl_mtx);
        g_ctrl.crossbow_distance_ok = has_valid_reverse_nfz;
        g_ctrl.crossbow_direction_confirmed =
        crossbow_direction_locked_ && has_valid_reverse_nfz;
      }

      // Keep OOR warning visible for at least 0.5s to avoid blink-disappear
      // when fetch/geometry momentarily drops nearest distance.
      {
        const auto now_tp = std::chrono::steady_clock::now();
        if (!has_valid_reverse_nfz && nearest_reverse_dist_m > 0.0) {
          crossbow_oor_hold_dist_m_ = nearest_reverse_dist_m;
          crossbow_oor_hold_until_tp_ = now_tp + std::chrono::milliseconds(500);
        } else if (!has_valid_reverse_nfz && nearest_reverse_dist_m <= 0.0 &&
                   crossbow_oor_hold_dist_m_ > 0.0 &&
                   now_tp < crossbow_oor_hold_until_tp_) {
          nearest_reverse_dist_m = crossbow_oor_hold_dist_m_;
        }
        if (has_valid_reverse_nfz || now_tp >= crossbow_oor_hold_until_tp_) {
          crossbow_oor_hold_dist_m_ = 0.0;
        }
      }

      QPoint center_px;
      if (lonlat_to_osm_screen(selected_lat_deg_, selected_lon_deg_, panel, &center_px)) {
        // ── Crossbow drone dots: AoA bearing/distance → OSM lat/lon → screen ─
        // Projects each confirmed, non-whitelisted target that has bearing+distance
        // onto the map as a red dot with a speed-history tail and distance label.
        auto draw_crossbow_target_dots = [&](const QRect &view_rect,
                                             int zoom_level,
                                             double panel_cx_px,
                                             double panel_cy_px) {
          if (!dji_detect_mgr_ || !osm_in.receiver_valid) return;
          const double recv_lat = osm_in.receiver_lat_deg;
          const double recv_lon = osm_in.receiver_lon_deg;
          const double cos_lat_d = std::max(0.15,
              std::cos(recv_lat * M_PI / 180.0));
          const double mpp_d = (40075016.0 * cos_lat_d) /
              (256.0 * std::pow(2.0, static_cast<double>(zoom_level)));
          const double ppm_d = (mpp_d > 1e-12) ? 1.0 / mpp_d : 1.0;
          const double world_sz_d = osm_world_size_for_zoom(zoom_level);
          const double left_d = panel_cx_px -
              static_cast<double>(view_rect.width()) * 0.5;
          const double top_d  = panel_cy_px -
              static_cast<double>(view_rect.height()) * 0.5;

          const auto dots = dji_detect_mgr_->targets_snapshot();
          p.save();
          p.setRenderHint(QPainter::Antialiasing, true);
          p.setClipRect(view_rect);

          for (const auto &t : dots) {
            if (!t.confirmed_dji || t.whitelisted) continue;
            if (!t.has_bearing || !t.has_distance || t.distance_m <= 0.0) continue;

            const double brg_rad_d = t.bearing_deg * M_PI / 180.0;
            const double dlat_d = (t.distance_m * std::cos(brg_rad_d)) / 111320.0;
            const double dlon_d = (t.distance_m * std::sin(brg_rad_d)) /
                                   (111320.0 * cos_lat_d);
            const double tgt_lat_d = recv_lat + dlat_d;
            const double tgt_lon_d = recv_lon + dlon_d;

            const double wx0_d = osm_lon_to_world_x(tgt_lon_d, zoom_level);
            const double wy_d  = osm_lat_to_world_y(tgt_lat_d, zoom_level);

            double wx_d = wx0_d;
            for (int k = -1; k <= 1; ++k) {
              const double wx_k = wx0_d + static_cast<double>(k) * world_sz_d;
              if (std::abs(wx_k - panel_cx_px) < std::abs(wx_d - panel_cx_px))
                wx_d = wx_k;
            }
            const int sx_d = view_rect.x() +
                             static_cast<int>(std::llround(wx_d - left_d));
            const int sy_d = view_rect.y() +
                             static_cast<int>(std::llround(wy_d - top_d));

            if (sx_d < view_rect.x() - 20 || sx_d > view_rect.right() + 20) continue;
            if (sy_d < view_rect.y() - 20 || sy_d > view_rect.bottom() + 20) continue;

            const QPointF dot_pt(static_cast<double>(sx_d),
                                 static_cast<double>(sy_d));

            // Speed-history tail (points backward from travel direction)
            if (t.has_velocity && t.speed_mps > 0.2) {
              const double hdg_rad_d = t.heading_deg * M_PI / 180.0;
              const double tail_px_d = std::min(40.0, t.speed_mps * ppm_d * 3.0);
              const QPointF tail_end_d(dot_pt.x() - tail_px_d * std::sin(hdg_rad_d),
                                       dot_pt.y() + tail_px_d * std::cos(hdg_rad_d));
              p.setPen(QPen(QColor(255, 100, 100, 150), 1.5));
              p.setBrush(Qt::NoBrush);
              p.drawLine(dot_pt, tail_end_d);
            }
            // Red hostile dot
            p.setPen(QPen(QColor(160, 0, 0, 230), 1.5));
            p.setBrush(QColor(240, 50, 50, 210));
            p.drawEllipse(dot_pt, 5.5, 5.5);
            // Distance label
            {
              QFont lbl_f;
              lbl_f.setPixelSize(10);
              lbl_f.setBold(true);
              p.setFont(lbl_f);
              p.setPen(QColor(255, 200, 200, 215));
              p.drawText(QPointF(dot_pt.x() + 8.0, dot_pt.y() + 4.0),
                         QString("%1m").arg(
                             static_cast<int>(std::llround(t.distance_m))));
            }
          }
          p.restore();
        };

        crossbow_draw_range_rings(p, panel, osm_zoom_, selected_lat_deg_, center_px);
        draw_crossbow_target_dots(panel, osm_zoom_,
                                  osm_center_px_x_, osm_center_px_y_);
      }
    } else {
      // No center chosen yet — show activation hint.
      sync_crossbow_ctrl_flags(false, false);
    }
  }
}