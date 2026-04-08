void MapWidget::draw_tutorial_overlay(QPainter &p, int win_width,
                                      int win_height) {
  bool running_ui = false;
  bool detailed = false;
  uint8_t signal_mode = 0;
  int sat_mode = 2;
  int single_candidate_count = 0;
  int active_prn_mask_len = 0;
  std::vector<int> single_candidates;
  std::vector<int> active_prn_mask;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    detailed = g_ctrl.show_detailed_ctrl;
    signal_mode = g_ctrl.signal_mode;
    sat_mode = g_ctrl.sat_mode;
    single_candidate_count = std::max(0, g_ctrl.single_candidate_count);
    const int cand_limit = std::min(single_candidate_count,
                                    (int)(sizeof(g_ctrl.single_candidates) /
                                          sizeof(g_ctrl.single_candidates[0])));
    single_candidates.reserve(cand_limit);
    for (int i = 0; i < cand_limit; ++i) {
      single_candidates.push_back(g_ctrl.single_candidates[i]);
    }
    }

  active_prn_mask_len = MAX_SAT;
  active_prn_mask.reserve(active_prn_mask_len);
  for (int i = 0; i < active_prn_mask_len; ++i) {
    active_prn_mask.push_back(g_active_prn_mask[i]);
  }

  TutorialOverlayInput overlay_in;
  overlay_in.host_widget = this;
  overlay_in.win_width = win_width;
  overlay_in.win_height = win_height;
  overlay_in.language = ui_language_;
  overlay_in.overlay_visible = tutorial_overlay_visible_;
  overlay_in.running_ui = running_ui;
  overlay_in.detailed = detailed;
  overlay_in.has_navigation_data = has_loaded_navigation_data();
  overlay_in.sat_mode = sat_mode;
  overlay_in.single_candidate_count = single_candidate_count;
  overlay_in.single_candidates = std::move(single_candidates);
  overlay_in.active_prn_mask_len = active_prn_mask_len;
  overlay_in.active_prn_mask = std::move(active_prn_mask);
  overlay_in.signal_mode = signal_mode;
  overlay_in.sat_points = sats_;
  overlay_in.step = tutorial_step_;
  overlay_in.osm_panel_rect = osm_panel_rect_;
  overlay_in.osm_stop_btn_rect = osm_stop_btn_rect_;
  overlay_in.osm_runtime_rect = osm_runtime_rect_;
  overlay_in.search_box_rect = search_box_ ? search_box_->geometry() : QRect();
  overlay_in.nfz_btn_rect = nfz_btn_rect_;
  overlay_in.dark_mode_btn_rect = dark_mode_btn_rect_;
  overlay_in.tutorial_toggle_rect = tutorial_toggle_rect_;
  overlay_in.lang_btn_rect = lang_btn_rect_;
  overlay_in.osm_status_badge_rects = osm_status_badge_rects_;
  if (tutorial_overlay_visible_ &&
      (tutorial_step_ == 5 || tutorial_step_ == 6 || tutorial_step_ == 7 || tutorial_step_ == 8)) {
    const bool force_detail = (tutorial_step_ >= 7);
    MapControlPanelInput panel_in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      panel_in.ctrl = g_ctrl;
    }
    panel_in.ctrl.show_detailed_ctrl = force_detail;
    panel_in.time_info = time_info_;
    panel_in.language = ui_language_;
    panel_in.control_text_scale = control_text_scale_;
    panel_in.caption_text_scale = control_caption_scale_;
    panel_in.switch_option_text_scale = control_switch_option_scale_;
    panel_in.value_text_scale = control_value_scale_;
    panel_in.accent_color = control_accent_color_;
    panel_in.border_color = control_border_color_;
    panel_in.text_color = control_text_color_;
    panel_in.dim_text_color = control_dim_text_color_;

    panel_in.rnx_name_bds = QString::fromUtf8(
      panel_in.ctrl.rinex_name_bds[0] ? panel_in.ctrl.rinex_name_bds : "N/A");
    panel_in.rnx_name_gps = QString::fromUtf8(
      panel_in.ctrl.rinex_name_gps[0] ? panel_in.ctrl.rinex_name_gps : "N/A");

    QPixmap signal_full(QSize(win_width, win_height));
    signal_full.fill(Qt::transparent);
    {
      QPainter signal_p(&signal_full);
      map_draw_control_panel(signal_p, win_width, win_height, panel_in);
    }

    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo, force_detail);
    QRect signal_rect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
    overlay_in.signal_clean_rect = signal_rect;
    overlay_in.signal_clean_snapshot = signal_full.copy(signal_rect);
  }
  if (tutorial_overlay_visible_ && tutorial_step_ == 4) {
    const QRect bottom_right_rect(win_width / 2, win_height / 2,
                                  win_width - win_width / 2,
                                  win_height - win_height / 2);
    QPixmap wave_clean(bottom_right_rect.size());
    wave_clean.fill(QColor(7, 17, 30, 255));
    {
      QPainter wave_p(&wave_clean);
      wave_p.translate(-bottom_right_rect.x(), -bottom_right_rect.y());
      draw_spectrum_panel(wave_p, win_width, win_height);
      draw_waterfall_panel(wave_p, win_width, win_height);
      draw_time_panel(wave_p, win_width, win_height);
      draw_constellation_panel(wave_p, win_width, win_height);
    }
    overlay_in.waveform_clean_rect = bottom_right_rect;
    overlay_in.waveform_clean_snapshot = wave_clean;
  }
  // Step 2: OSM lower-half clean snapshot with STOP / RUN TIME preview active.
  // Re-renders the OSM panel overlay (no tile fetch) into an offscreen pixmap so
  // the frozen session background is not used and show_tutorial_stop_preview fires.
  if (tutorial_overlay_visible_ && tutorial_step_ == 2 && !osm_panel_rect_.isEmpty()) {
    bool jam_sel = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      jam_sel = (g_ctrl.interference_selection == 1);
    }
    std::vector<MapOsmPanelSegment> snap_segs;
    {
      std::lock_guard<std::mutex> lk(g_path_seg_mtx);
      snap_segs.reserve(g_path_segments.size());
      for (const auto &seg : g_path_segments) {
        MapOsmPanelSegment c;
        c.start_lat_deg = seg.start_lat_deg;
        c.start_lon_deg = seg.start_lon_deg;
        c.end_lat_deg   = seg.end_lat_deg;
        c.end_lon_deg   = seg.end_lon_deg;
        c.mode          = map_osm_panel_path_mode_from_int(seg.mode);
        c.state         = map_osm_panel_segment_state_from_int(seg.state);
        c.polyline      = seg.polyline;
        snap_segs.push_back(std::move(c));
      }
    }

    QPixmap osm_clean(QSize(win_width, win_height));
    osm_clean.fill(QColor("#081425"));
    MapOsmPanelState snap_out;
    {
      QPainter osm_p(&osm_clean);
      if (!osm_bg_cache_.isNull()) {
        osm_p.drawImage(osm_panel_rect_.topLeft(), osm_bg_cache_);
      }
      const bool dji_on = dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled();
      MapOsmPanelInput snap_in;
      snap_in.panel                   = osm_panel_rect_;
      snap_in.language                = ui_language_;
      snap_in.osm_zoom                = osm_zoom_;
      snap_in.osm_zoom_base           = osm_zoom_base_;
      snap_in.osm_center_px_x         = osm_center_px_x_;
      snap_in.osm_center_px_y         = osm_center_px_y_;
      snap_in.tile_cache              = nullptr;
      snap_in.path_segments           = &snap_segs;
      snap_in.preview_polyline        = &preview_polyline_;
      snap_in.nfz_zones               = dji_on ? &dji_nfz_mgr_->get_zones() : nullptr;
      snap_in.coord_to_screen         = [this](double lat, double lon, QPoint *out) {
        return this->lonlat_to_osm_screen(lat, lon, osm_panel_rect_, out);
      };
      snap_in.nfz_enabled             = dji_on;
      snap_in.has_selected_llh        = has_selected_llh_;
      snap_in.selected_lat_deg        = selected_lat_deg_;
      snap_in.selected_lon_deg        = selected_lon_deg_;
      snap_in.has_preview_segment     = has_preview_segment_;
      snap_in.preview_mode            = map_osm_panel_path_mode_from_int(preview_mode_);
      snap_in.preview_start_lat_deg   = preview_start_lat_deg_;
      snap_in.preview_start_lon_deg   = preview_start_lon_deg_;
      snap_in.preview_end_lat_deg     = preview_end_lat_deg_;
      snap_in.preview_end_lon_deg     = preview_end_lon_deg_;
      snap_in.receiver_valid          = g_receiver_valid != 0;
      snap_in.receiver_lat_deg        = g_receiver_lat_deg;
      snap_in.receiver_lon_deg        = g_receiver_lon_deg;
      snap_in.running_ui              = false;
      snap_in.jam_selected            = jam_sel;
      snap_in.can_undo                = false;
      snap_in.dark_map_mode           = dark_map_mode_;
      snap_in.tutorial_enabled         = false;
      snap_in.tutorial_overlay_visible = false;
      snap_in.tutorial_step            = 2;
      snap_in.force_stop_preview       = true;
      snap_in.nfz_enabled              = true;  // force legend visible for guide anchors
      snap_in.nfz_zones                = nullptr;
      snap_in.show_search_return      = false;
      snap_in.search_box_rect         = QRect();
      snap_in.tx_active               = false;
      snap_in.elapsed_sec             = 0;
      map_draw_osm_panel_overlay(osm_p, snap_in, &snap_out);
    }
    const QRect left_map = osm_panel_rect_.adjusted(6, 6, -6, -6);
    const int half_h = std::max(12, left_map.height() / 2);
    const QRect lower_rect(left_map.x(), left_map.y() + half_h,
                           left_map.width(),
                           std::max(12, left_map.height() - half_h));
    overlay_in.osm_lower_clean_rect     = lower_rect;
    overlay_in.osm_lower_clean_snapshot = osm_clean.copy(lower_rect);
    if (!snap_out.status_badge_rects.empty())
      overlay_in.osm_status_badge_rects = snap_out.status_badge_rects;
    if (!snap_out.nfz_legend_row_rects.empty())
      overlay_in.osm_nfz_legend_row_rects = snap_out.nfz_legend_row_rects;
  }
  overlay_in.last_step = tutorial_last_step();

  TutorialOverlayState overlay_state;
  overlay_state.prev_btn_rect = tutorial_prev_btn_rect_;
  overlay_state.next_btn_rect = tutorial_next_btn_rect_;
  overlay_state.close_btn_rect = tutorial_close_btn_rect_;
  overlay_state.anim_step_anchor = tutorial_anim_step_anchor_;
  overlay_state.anim_start_tp = tutorial_anim_start_tp_;
  overlay_state.spotlight_index = tutorial_spotlight_index_;
  overlay_state.text_page = tutorial_text_page_;
  overlay_state.text_page_count = tutorial_text_page_count_;
  overlay_state.text_page_anchor_step = tutorial_text_page_anchor_step_;
  overlay_state.text_page_anchor_spotlight =
      tutorial_text_page_anchor_spotlight_;
  for (int i = 0; i < 5; ++i) {
    overlay_state.toc_btn_rects[i] = tutorial_toc_btn_rects_[i];
    overlay_state.toc_btn_targets[i] = tutorial_toc_btn_targets_[i];
  }
  overlay_state.contents_btn_rect   = tutorial_contents_btn_rect_;
  overlay_state.callout_hit_boxes   = tutorial_callout_hit_boxes_;
  overlay_state.callout_hit_anchors = tutorial_callout_hit_anchors_;
  overlay_state.has_glow            = tutorial_has_glow_;
  overlay_state.glow_anchor         = tutorial_glow_anchor_;
  overlay_state.glow_step           = tutorial_glow_step_;
  overlay_state.glow_start_tp       = tutorial_glow_start_tp_;

  tutorial_draw_overlay(p, overlay_in, &overlay_state);

  tutorial_prev_btn_rect_ = overlay_state.prev_btn_rect;
  tutorial_next_btn_rect_ = overlay_state.next_btn_rect;
  tutorial_close_btn_rect_ = overlay_state.close_btn_rect;
  tutorial_anim_step_anchor_ = overlay_state.anim_step_anchor;
  tutorial_anim_start_tp_ = overlay_state.anim_start_tp;
  tutorial_spotlight_index_ = overlay_state.spotlight_index;
  tutorial_text_page_ = overlay_state.text_page;
  tutorial_text_page_count_ = overlay_state.text_page_count;
  tutorial_text_page_anchor_step_ = overlay_state.text_page_anchor_step;
  tutorial_text_page_anchor_spotlight_ =
      overlay_state.text_page_anchor_spotlight;
  for (int i = 0; i < 5; ++i) {
    tutorial_toc_btn_rects_[i] = overlay_state.toc_btn_rects[i];
    tutorial_toc_btn_targets_[i] = overlay_state.toc_btn_targets[i];
  }
  tutorial_contents_btn_rect_   = overlay_state.contents_btn_rect;
  tutorial_callout_hit_boxes_   = overlay_state.callout_hit_boxes;
  tutorial_callout_hit_anchors_ = overlay_state.callout_hit_anchors;
  tutorial_has_glow_            = overlay_state.has_glow;
  tutorial_glow_anchor_         = overlay_state.glow_anchor;
  tutorial_glow_step_           = overlay_state.glow_step;
  tutorial_glow_start_tp_       = overlay_state.glow_start_tp;
}
