void MapWidget::onTileReply(QNetworkReply *reply) {
  if (!reply)
    return;

  QString key = reply->property("tile_key").toString();
  bool tile_inserted = false;

  if (!key.isEmpty()) {
    tile_pending_.erase(key);
    if (reply->error() == QNetworkReply::NoError) {
      QPixmap px;
      QByteArray bytes = reply->readAll();
      if (px.loadFromData(bytes)) {
        map_tile_store_cache_item(&tile_cache_, &tile_cache_order_, key,
                                  std::move(px));
        tile_inserted = true;
        osm_bg_needs_redraw_ = true;
      }
    }
  }

  reply->deleteLater();
  request_visible_tiles();
  if (tile_inserted && !osm_panel_rect_.isEmpty())
    update(osm_panel_rect_);
}

void MapWidget::set_selected_llh_from_point(const QPoint &pos) {
  double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
  double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
  double local_x = (double)(pos.x() - osm_panel_rect_.x());
  double local_y = (double)(pos.y() - osm_panel_rect_.y());
  double world_x = left + local_x;
  double world_y = top + local_y;

  selected_lat_deg_ = osm_world_y_to_lat(world_y, osm_zoom_);
  selected_lon_deg_ = osm_world_x_to_lon(world_x, osm_zoom_);
  selected_h_m_ = 0.0;
  has_selected_llh_ = true;
  reset_crossbow_direction_state();

  sync_receiver_marker_to_selected_llh();

  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.llh_ready = true;
    g_ctrl.crossbow_direction_confirmed = false;
    g_ctrl.crossbow_distance_ok = false;
  }

  {
    std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
    g_llh_pick_lat_deg = selected_lat_deg_;
    g_llh_pick_lon_deg = selected_lon_deg_;
    g_llh_pick_h_m = selected_h_m_;
  }
  g_gui_llh_pick_req.fetch_add(1);

  // Force immediate UI + NFZ refresh so status coordinates never appear stale
  // after clicking a new map point.
  request_visible_tiles();
  notify_nfz_viewport_changed();
  osm_bg_needs_redraw_ = true;
  if (!osm_panel_rect_.isEmpty()) {
    update(osm_panel_rect_);
  }
}

void MapWidget::reset_crossbow_direction_state() {
  crossbow_has_mouse_ = false;
  crossbow_mouse_pos_ = QPoint();
  crossbow_auto_overview_done_ = false;
  crossbow_direction_locked_ = false;
  crossbow_locked_bearing_deg_ = 0.0;
  crossbow_cached_nfz_valid_ = false;
  crossbow_far_fetch_done_ = false;
  crossbow_oor_hold_dist_m_ = 0.0;
  crossbow_oor_hold_until_tp_ = std::chrono::steady_clock::time_point{};
}

void MapWidget::sync_crossbow_ctrl_flags(bool direction_confirmed,
                                         bool distance_ok) {
  std::lock_guard<std::mutex> lk(g_ctrl_mtx);
  g_ctrl.crossbow_direction_confirmed = direction_confirmed;
  g_ctrl.crossbow_distance_ok = distance_ok;
}

void MapWidget::notify_nfz_viewport_changed() {
  if (!dji_nfz_mgr_ || !dji_nfz_mgr_->is_enabled() ||
      osm_panel_rect_.width() == 0)
    return;
  double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
  double right = left + (double)osm_panel_rect_.width();
  double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
  double bottom = top + (double)osm_panel_rect_.height();

  double lat_top = osm_world_y_to_lat(top, osm_zoom_);
  double lat_bot = osm_world_y_to_lat(bottom, osm_zoom_);
  double lon_left = osm_world_x_to_lon(left, osm_zoom_);
  double lon_right = osm_world_x_to_lon(right, osm_zoom_);

  dji_nfz_mgr_->trigger_fetch(lon_left, lon_right, lat_top, lat_bot,
                              osm_zoom_);
}

void MapWidget::sync_receiver_marker_to_selected_llh() {
  const double snapped_lon = wrap_lon_deg(selected_lon_deg_);
  const auto now = std::chrono::steady_clock::now();

  g_receiver_lat_deg = selected_lat_deg_;
  g_receiver_lon_deg = snapped_lon;
  g_receiver_valid = 1;

  receiver_anim_valid_ = true;
  receiver_anim_lat_deg_ = selected_lat_deg_;
  receiver_anim_lon_deg_ = snapped_lon;
  receiver_anim_target_lat_deg_ = selected_lat_deg_;
  receiver_anim_target_lon_deg_ = snapped_lon;
  receiver_anim_last_tp_ = now;
  next_receiver_draw_tick_ = now;
}

void MapWidget::set_selected_llh_direct(double lat_deg, double lon_deg,
                                        double h_m, bool recenter_map) {
  selected_lat_deg_ = lat_deg;
  selected_lon_deg_ = lon_deg;
  selected_h_m_ = h_m;
  has_selected_llh_ = true;
  reset_crossbow_direction_state();
  sync_receiver_marker_to_selected_llh();

  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.llh_ready = true;
    g_ctrl.crossbow_direction_confirmed = false;
    g_ctrl.crossbow_distance_ok = false;
  }
  {
    std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
    g_llh_pick_lat_deg = selected_lat_deg_;
    g_llh_pick_lon_deg = selected_lon_deg_;
    g_llh_pick_h_m = selected_h_m_;
  }
  g_gui_llh_pick_req.fetch_add(1);

  if (recenter_map) {
    osm_center_px_x_ = osm_lon_to_world_x(lon_deg, osm_zoom_);
    osm_center_px_y_ = osm_lat_to_world_y(lat_deg, osm_zoom_);
    normalize_osm_center();
    request_visible_tiles();
  }
}
