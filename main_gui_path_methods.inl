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
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    jam_selected = (g_ctrl.interference_selection == 1);
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
  osm_in.panel = panel;
  osm_in.language = ui_language_;
  osm_in.osm_zoom = osm_zoom_;
  osm_in.osm_zoom_base = osm_zoom_base_;
  osm_in.osm_center_px_x = osm_center_px_x_;
  osm_in.osm_center_px_y = osm_center_px_y_;
  osm_in.tile_cache = &tile_cache_;
  osm_in.path_segments = &segs;
  osm_in.preview_polyline = &preview_polyline_;
  osm_in.nfz_zones = (dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled()) ? &dji_nfz_mgr_->get_zones() : nullptr;
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
  osm_in.receiver_valid = g_receiver_valid != 0;
  osm_in.receiver_lat_deg = g_receiver_lat_deg;
  osm_in.receiver_lon_deg = g_receiver_lon_deg;
  osm_in.running_ui = running_ui;
  osm_in.jam_selected = jam_selected;
  osm_in.can_undo = can_undo;
  osm_in.dark_map_mode = dark_map_mode_;
  osm_in.tutorial_enabled = tutorial_enabled_;
  osm_in.tutorial_overlay_visible = tutorial_overlay_visible_;
  osm_in.tutorial_step = tutorial_step_;
  osm_in.show_search_return = show_search_return_;
  osm_in.search_box_rect = search_box_ ? search_box_->geometry() : QRect();
  osm_in.tx_active = tx_active;
  osm_in.elapsed_sec = elapsed_sec;
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
  osm_runtime_rect_ = osm_out.osm_runtime_rect;
  osm_status_badge_rects_ = osm_out.status_badge_rects;
}