#ifndef MAIN_GUI_INPUT_EVENT_METHODS_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
void MapWidget::mousePressEvent(QMouseEvent *event) {
  bool running_ui = false;
  bool detailed = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    detailed = g_ctrl.show_detailed_ctrl;
  }

  if (event->button() == Qt::LeftButton) {
    const bool tutorial_was_visible = tutorial_overlay_visible_;

    if (tutorial_overlay_visible_ && tutorial_step_ == 0) {
      for (int i = 0; i < 5; ++i) {
        if (!tutorial_toc_btn_rects_[i].isNull() &&
            tutorial_toc_btn_rects_[i].contains(event->pos())) {
          tutorial_step_ = tutorial_toc_btn_targets_[i];
          tutorial_text_page_ = 0;
          tutorial_anim_step_anchor_ = -1;
          tutorial_spotlight_index_ = 0;
          update();
          event->accept();
          return;
        }
      }
    }

    if (tutorial_overlay_visible_ && !tutorial_contents_btn_rect_.isNull() &&
        tutorial_contents_btn_rect_.contains(event->pos())) {
      tutorial_step_ = 0;
      tutorial_text_page_ = 0;
      tutorial_anim_step_anchor_ = -1;
      tutorial_has_glow_ = false;
      update();
      event->accept();
      return;
    }

    if (tutorial_overlay_visible_) {
      for (int i = 0; i < (int)tutorial_callout_hit_boxes_.size(); ++i) {
        if (tutorial_callout_hit_boxes_[i].contains(QPointF(event->pos()))) {
          tutorial_has_glow_ = true;
          tutorial_glow_anchor_ = tutorial_callout_hit_anchors_[i];
          tutorial_glow_start_tp_ = std::chrono::steady_clock::now();
          tutorial_glow_step_ = tutorial_step_;
          update();
          event->accept();
          return;
        }
      }
    }

    if (tutorial_handle_click(event->pos(), running_ui, tutorial_last_step(),
                              tutorial_toggle_rect_, tutorial_prev_btn_rect_,
                              tutorial_next_btn_rect_, tutorial_close_btn_rect_,
                              &tutorial_enabled_, &tutorial_overlay_visible_,
                              &tutorial_step_, &tutorial_anim_step_anchor_,
                              &tutorial_spotlight_index_, &tutorial_text_page_,
                              &tutorial_text_page_count_,
                              &tutorial_text_page_anchor_step_,
                              &tutorial_text_page_anchor_spotlight_)) {
      if (!tutorial_was_visible && tutorial_overlay_visible_) {
        wf_.image = QImage();
        wf_.width = 0;
        wf_.height = 0;
        g_gui_reset_waterfall_req.fetch_add(1);
        update(right_bottom_region());
      }
      update();
      event->accept();
      return;
    }

    if (inline_editor_ && inline_editor_->isVisible() &&
        !inline_editor_->geometry().contains(event->pos())) {
      commit_inline_edit(true);
    }

    if (search_results_list_ && search_results_list_->isVisible()) {
      const QRect list_rect = search_results_list_->geometry();
      const QRect box_rect = search_box_ ? search_box_->geometry() : QRect();
      if (map_overlay_click_outside_search(list_rect, box_rect,
                                           event->pos())) {
        hide_search_results();
      }
    }

    if (lang_btn_rect_.contains(event->pos())) {
      ui_language_ = gui_runtime_toggle_language(ui_language_);
      gui_runtime_apply_search_placeholder(search_box_, ui_language_);
      gui_runtime_apply_language_font(this, ui_language_);
      update_alert_overlay();
      update();
      event->accept();
      return;
    }

    if (control_gear_btn_rect_.contains(event->pos())) {
      on_control_gear_click();
      event->accept();
      return;
    }

    const bool zh = gui_language_is_zh_tw(ui_language_);
    auto normalize_token = [](const QString &value) {
      return value.trimmed().toLower().replace('-', ':');
    };
    auto push_crossbow_alert = [&](int level, const QString &zh_text, const QString &en_text) {
      const QByteArray utf8 = (zh ? zh_text : en_text).toUtf8();
      map_gui_push_alert(level, utf8.constData());
    };
    auto current_bridge_csv = [&]() {
      std::lock_guard<std::mutex> lk(g_gui_wifi_rid_applied_mtx);
      if (g_gui_wifi_rid_applied_initialized) {
        return QString::fromStdString(g_gui_wifi_rid_applied_csv).trimmed();
      }
      const char *allow_env = std::getenv("BDS_WIFI_RID_ALLOW_IDS");
      return QString::fromUtf8(allow_env ? allow_env : "").trimmed();
    };
    auto set_bridge_csv = [&](const QString &csv) {
      const QString trimmed = csv.trimmed();
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_allow_mtx);
        g_gui_wifi_rid_allow_csv = trimmed.toStdString();
      }
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_applied_mtx);
        g_gui_wifi_rid_applied_csv = trimmed.toStdString();
        g_gui_wifi_rid_applied_initialized = true;
      }
      g_gui_wifi_rid_allow_apply_req.fetch_add(1);
    };
    auto set_block_bridge_csv = [&](const QString &csv) {
      const QString trimmed = csv.trimmed();
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_block_mtx);
        g_gui_wifi_rid_block_csv = trimmed.toStdString();
      }
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_block_applied_mtx);
        g_gui_wifi_rid_block_applied_csv = trimmed.toStdString();
        g_gui_wifi_rid_block_applied_initialized = true;
      }
      g_gui_wifi_rid_block_apply_req.fetch_add(1);
    };
    auto current_bridge_mode_mixed = [&]() {
      std::lock_guard<std::mutex> lk(g_gui_wifi_rid_mode_applied_mtx);
      if (g_gui_wifi_rid_mode_applied_initialized) {
        return g_gui_wifi_rid_mode_mixed_applied;
      }
      const char *mode_env = std::getenv("BDS_WIFI_RID_WIFI_MODE");
      if (mode_env && mode_env[0] != '\0') {
        const QString mode = QString::fromUtf8(mode_env).trimmed().toLower();
        return mode == QStringLiteral("mixed");
      }
      const char *mixed_env = std::getenv("BDS_WIFI_RID_MIXED_ENABLE");
      if (!mixed_env || mixed_env[0] == '\0') {
        return true;
      }
      const QString flag = QString::fromUtf8(mixed_env).trimmed().toLower();
      return flag == QStringLiteral("1") || flag == QStringLiteral("true") ||
             flag == QStringLiteral("yes") || flag == QStringLiteral("on");
    };
    auto set_bridge_mode_mixed = [&](bool mixed_enabled) {
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_mode_mtx);
        g_gui_wifi_rid_mode_mixed_enabled = mixed_enabled;
      }
      {
        std::lock_guard<std::mutex> lk(g_gui_wifi_rid_mode_applied_mtx);
        g_gui_wifi_rid_mode_mixed_applied = mixed_enabled;
        g_gui_wifi_rid_mode_applied_initialized = true;
      }
      g_gui_wifi_rid_mode_apply_req.fetch_add(1);
    };

    if (!crossbow_tab_original_rect_.isNull() &&
        crossbow_tab_original_rect_.contains(event->pos())) {
      if (crossbow_view_mode_ != 0) {
        crossbow_view_mode_ = 0;
        crossbow_stage_page_ = 0;
      }
      update(right_bottom_region());
      event->accept();
      return;
    }

    // Whitelist/Blacklist/Bridge tabs removed from UI.

    if (!crossbow_sort_btn_rect_.isNull() &&
        crossbow_sort_btn_rect_.contains(event->pos())) {
      crossbow_stage_sort_mode_ = (crossbow_stage_sort_mode_ + 1) % 3;
      crossbow_stage_page_ = 0;
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_page_prev_btn_rect_.isNull() &&
        crossbow_page_prev_btn_rect_.contains(event->pos())) {
      if (crossbow_stage_page_ > 0) {
        crossbow_stage_page_ -= 1;
      }
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_page_next_btn_rect_.isNull() &&
        crossbow_page_next_btn_rect_.contains(event->pos())) {
      if (crossbow_stage_page_ + 1 < crossbow_stage_total_pages_) {
        crossbow_stage_page_ += 1;
      }
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_whitelist_clear_btn_rect_.isNull() &&
        crossbow_whitelist_clear_btn_rect_.contains(event->pos())) {
      if (dji_detect_mgr_) {
        dji_detect_mgr_->clear_whitelist();
      }
      push_crossbow_alert(1,
                          QString::fromUtf8("已清除 GUI 白名單"),
                          QString("GUI whitelist cleared"));
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_whitelist_unsync_btn_rect_.isNull() &&
        crossbow_whitelist_unsync_btn_rect_.contains(event->pos())) {
      if (dji_detect_mgr_) {
        dji_detect_mgr_->clear_whitelist();
      }
      set_bridge_csv(QString());
      push_crossbow_alert(1,
                          QString::fromUtf8("已重設 Capture Filter，同步關閉"),
                          QString("Capture filter reset and sync removed"));
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_blacklist_clear_btn_rect_.isNull() &&
        crossbow_blacklist_clear_btn_rect_.contains(event->pos())) {
      if (dji_detect_mgr_) {
        dji_detect_mgr_->clear_blacklist();
      }
      set_block_bridge_csv(QString());
      push_crossbow_alert(1,
                          QString::fromUtf8("已清除 GUI 黑名單，並同步關閉 Bridge 黑名單"),
                          QString("GUI blacklist cleared and Bridge blacklist synced off"));
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_wifi_allow_apply_btn_rect_.isNull() &&
        crossbow_wifi_allow_apply_btn_rect_.contains(event->pos())) {
      QString csv;
      if (dji_detect_mgr_) {
        const auto items = dji_detect_mgr_->whitelist_items();
        QStringList toks;
        toks.reserve((int)items.size());
        for (const auto &item : items) {
          const QString key = item.trimmed();
          if (!key.isEmpty()) {
            toks.push_back(key);
          }
        }
        csv = toks.join(',');
      }
      set_bridge_csv(csv);
      push_crossbow_alert(1,
                          csv.isEmpty()
                    ? QString::fromUtf8("Display Filter 為空，Capture 同步關閉")
                    : QString::fromUtf8("已套用 Display Filter 並同步 Capture"),
                          csv.isEmpty()
                    ? QString("Display filter empty, capture sync off")
                    : QString("Display filter applied and capture synced"));
      update(right_bottom_region());
      event->accept();
      return;
    }

    if (!crossbow_bridge_mode_checkbox_rect_.isNull() &&
        crossbow_bridge_mode_checkbox_rect_.contains(event->pos())) {
      const bool next_mixed_mode = !current_bridge_mode_mixed();
      set_bridge_mode_mixed(next_mixed_mode);
      push_crossbow_alert(1,
                          next_mixed_mode
            ? QString::fromUtf8("已切換為混合掃描模式（單卡相容）")
            : QString::fromUtf8("已切換為僅 RID 模式（單卡相容）"),
                          next_mixed_mode
            ? QString("Capture mode switched to mixed scan (single-NIC compatibility)")
            : QString("Capture mode switched to RID-only (single-NIC compatibility)"));
      update(right_bottom_region());
      event->accept();
      return;
    }

    for (const auto &hit : crossbow_whitelist_hit_rows_) {
      if (!hit.btn_rect.isNull() && hit.btn_rect.contains(event->pos())) {
        if (hit.action_kind == 2) {
          QStringList keep;
          const QString remove_key = normalize_token(hit.action_key);
          const QStringList tokens = current_bridge_csv().split(',', Qt::SkipEmptyParts);
          for (const auto &token : tokens) {
            if (normalize_token(token) != remove_key) {
              keep.push_back(token.trimmed());
            }
          }
          if (dji_detect_mgr_) {
            const auto items = dji_detect_mgr_->whitelist_items();
            QStringList whitelist_keep;
            for (const auto &item : items) {
              if (normalize_token(item) != remove_key) {
                whitelist_keep.push_back(item);
              }
            }
            dji_detect_mgr_->set_whitelist_csv(whitelist_keep.join(','));
          }
          set_bridge_csv(keep.join(','));
          push_crossbow_alert(1,
                              QString::fromUtf8("已從 Bridge 移除: %1").arg(hit.action_key),
                              QString("Removed from Bridge: %1").arg(hit.action_key));
        } else if (hit.action_kind == 3) {
          if (dji_detect_mgr_) {
            const auto items = dji_detect_mgr_->blacklist_items();
            QStringList keep;
            const QString remove_key = normalize_token(hit.action_key);
            for (const auto &item : items) {
              if (normalize_token(item) != remove_key) {
                keep.push_back(item);
              }
            }
            dji_detect_mgr_->set_blacklist_csv(keep.join(','));
            set_block_bridge_csv(keep.join(','));
            push_crossbow_alert(1,
                                QString::fromUtf8("已從黑名單移除: %1").arg(hit.action_key),
                                QString("Removed from blacklist: %1").arg(hit.action_key));
          }
        } else if (dji_detect_mgr_) {
          if (hit.action_kind == 1 || hit.currently_whitelisted) {
            const auto items = dji_detect_mgr_->whitelist_items();
            QStringList keep;
            const QString remove_key = normalize_token(hit.action_key);
            for (const auto &item : items) {
              if (normalize_token(item) != remove_key) {
                keep.push_back(item);
              }
            }
            dji_detect_mgr_->set_whitelist_csv(keep.join(','));
            push_crossbow_alert(1,
                                QString::fromUtf8("已從白名單移除: %1").arg(hit.action_key),
                                QString("Removed from whitelist: %1").arg(hit.action_key));
          } else if (hit.action_kind == 0) {
            const auto items = dji_detect_mgr_->blacklist_items();
            const QString key = normalize_token(hit.action_key);
            bool already = false;
            for (const auto &item : items) {
              if (normalize_token(item) == key) {
                already = true;
                break;
              }
            }
            if (!already) {
              dji_detect_mgr_->add_blacklist_id(hit.action_key);
              const auto merged = dji_detect_mgr_->blacklist_items();
              QStringList merged_csv;
              for (const auto &item : merged) {
                if (!item.trimmed().isEmpty()) {
                  merged_csv.push_back(item.trimmed());
                }
              }
              set_block_bridge_csv(merged_csv.join(','));
              push_crossbow_alert(1,
                                  QString::fromUtf8("已加入黑名單: %1").arg(hit.action_key),
                                  QString("Added to blacklist: %1").arg(hit.action_key));
            }
          }
        }
        update(right_bottom_region());
        event->accept();
        return;
      }
    }

    if (!crossbow_stage_launch_btn_rect_.isNull() &&
        crossbow_stage_launch_btn_rect_.contains(event->pos())) {
      g_gui_launch_req.fetch_add(1);
      update(right_bottom_region());
      event->accept();
      return;
    }

    int value_field = control_value_hit_test(
        event->pos().x(), event->pos().y(), width(), height(), detailed);
    if (value_field >= 0) {
      begin_inline_edit(value_field);
      event->accept();
      return;
    }

    int slider_id = control_slider_hit_test(
        event->pos().x(), event->pos().y(), width(), height(), detailed);
    if (slider_id >= 0) {
      active_control_slider_ = slider_id;
      if (handle_control_slider_drag(active_control_slider_, event->pos().x(),
                                     width(), height())) {
        update(osm_panel_rect_);
        update(right_bottom_region());
      }
      event->accept();
      return;
    }

    MapOsmPressRects rects;
    rects.osm_panel_rect = osm_panel_rect_;
    rects.osm_scale_bar_rect = osm_scale_bar_rect_;
    rects.osm_stop_btn_rect = osm_stop_btn_rect_;
    rects.osm_launch_btn_rect = osm_launch_btn_rect_;
    rects.dark_mode_btn_rect = dark_mode_btn_rect_;
    rects.search_return_btn_rect = search_return_btn_rect_;
    rects.nfz_btn_rect = nfz_btn_rect_;
    rects.back_btn_rect = back_btn_rect_;
    rects.recenter_btn_rect = osm_recenter_btn_rect_;
    rects.nfz_legend_row_rects = osm_nfz_legend_row_rects_;
    rects.show_search_return = show_search_return_;

    MapOsmPressState state;
    state.dragging_osm = &dragging_osm_;
    state.drag_moved_osm = &drag_moved_osm_;
    state.drag_last_pos = &drag_last_pos_;

    MapOsmPressActions actions;
    actions.toggle_scale_ruler = [this]() {
      scale_ruler_enabled_ = !scale_ruler_enabled_;
      if (!scale_ruler_enabled_) {
        scale_ruler_has_start_ = false;
        scale_ruler_has_end_ = false;
        scale_ruler_end_fixed_ = false;
      }
    };
    actions.stop_simulation = [this]() {
      {
        std::lock_guard<std::mutex> lk(g_ctrl_mtx);
        g_ctrl.running_ui = false;
      }
      g_runtime_abort = 1;
      g_gui_reset_waterfall_req.fetch_add(1);
      g_gui_stop_req.fetch_add(1);
      osm_bg_needs_redraw_ = true;
    };
    actions.launch_signal = [this]() {
      map_gui_reset_monitor_views();
      g_gui_launch_req.fetch_add(1);
      osm_bg_needs_redraw_ = true;
    };
    actions.toggle_dark_mode = [this]() {
      dark_map_mode_ = !dark_map_mode_;
      osm_bg_needs_redraw_ = true;
    };
    actions.restore_search = [this]() {
      osm_zoom_ = pre_search_zoom_;
      osm_center_px_x_ = pre_search_center_x_;
      osm_center_px_y_ = pre_search_center_y_;
      user_map_interacted_ = true;
      show_search_return_ = false;
      has_selected_llh_ = false;
      hide_search_results();
      osm_bg_needs_redraw_ = true;
      normalize_osm_center();
      request_visible_tiles();
      notify_nfz_viewport_changed();
    };
    actions.toggle_nfz = [this]() {
      if (!dji_nfz_mgr_) return;
      bool current_state = dji_nfz_mgr_->is_enabled();
      dji_nfz_mgr_->set_enabled(!current_state);
      if (!current_state) {
        notify_nfz_viewport_changed();
      }
      osm_bg_needs_redraw_ = true;
    };
    actions.try_undo_last_segment = [this]() { try_undo_last_segment(); };
    actions.recenter_to_current_point = [this]() {
      if (fit_paths_into_view()) {
        request_visible_tiles();
        notify_nfz_viewport_changed();
        osm_bg_needs_redraw_ = true;
        update(osm_panel_rect_);
        return;
      }
      if (!(receiver_anim_valid_ || (g_receiver_valid != 0))) {
        return;
      }
      const double now_lat = receiver_anim_valid_ ? receiver_anim_lat_deg_ : g_receiver_lat_deg;
      const double now_lon = receiver_anim_valid_ ? receiver_anim_lon_deg_ : g_receiver_lon_deg;
      if (now_lat < -90.0 || now_lat > 90.0 || now_lon < -180.0 || now_lon > 180.0) {
        return;
      }
      osm_center_px_x_ = osm_lon_to_world_x(now_lon, osm_zoom_);
      osm_center_px_y_ = osm_lat_to_world_y(now_lat, osm_zoom_);
      normalize_osm_center();
      request_visible_tiles();
      notify_nfz_viewport_changed();
      osm_bg_needs_redraw_ = true;
      update(osm_panel_rect_);
    };
    actions.confirm_preview_segment = [this]() { confirm_preview_segment(); };
    actions.update_all = [this]() {
      update(osm_panel_rect_);
      update(right_bottom_region());
    };
    actions.update_rect = [this](const QRect &rect) { update(rect); };

    if (map_osm_handle_press(event->pos(), event->button(), running_ui,
                             is_jam_map_locked(), rects, &state, actions)) {
      event->accept();
      return;
    }

    if (handle_control_click(event->pos().x(), event->pos().y(), width(),
                             height())) {
      update(osm_panel_rect_);
      update(right_bottom_region());
    }
  } else {
    MapOsmPressRects rects;
    rects.osm_panel_rect = osm_panel_rect_;
    rects.osm_scale_bar_rect = osm_scale_bar_rect_;
    MapOsmPressState state;
    MapOsmPressActions actions;
    actions.toggle_scale_ruler = [this]() {
      scale_ruler_enabled_ = !scale_ruler_enabled_;
      if (!scale_ruler_enabled_) {
        scale_ruler_has_start_ = false;
        scale_ruler_has_end_ = false;
        scale_ruler_end_fixed_ = false;
      }
    };
    actions.confirm_preview_segment = [this]() { confirm_preview_segment(); };
    actions.update_rect = [this](const QRect &rect) { update(rect); };
    if (map_osm_handle_press(event->pos(), event->button(), running_ui,
                             is_jam_map_locked(), rects, &state, actions)) {
      event->accept();
      return;
    }
  }
  QWidget::mousePressEvent(event);
}

void MapWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  bool running_ui = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
  }
  if (map_osm_handle_double_click(
          event->pos(), event->button() == Qt::LeftButton, osm_panel_rect_,
          is_jam_map_locked(), running_ui, &suppress_left_click_release_,
          [this](const QPoint &pos) { set_preview_target(pos, PATH_MODE_PLAN); },
          [this](const QRect &rect) { update(rect); })) {
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void MapWidget::mouseMoveEvent(QMouseEvent *event) {
  const bool tutorial_hovered =
      !tutorial_toggle_rect_.isNull() && tutorial_toggle_rect_.contains(event->pos());
  if (tutorial_toggle_hovered_ != tutorial_hovered) {
    tutorial_toggle_hovered_ = tutorial_hovered;
    update(tutorial_toggle_rect_.adjusted(-12, -12, 12, 12));
  }

  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);

    if (g_ctrl.running_ui) {
      int win_width = width();
      int win_height = height();
      int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;

      get_rb_lq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                    &panel_w, &panel_h, false, 1.0);
      QRect lb_panel_upper(panel_x, panel_y, panel_w, panel_h);

      get_rb_lq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                    &panel_w, &panel_h, true, 1.0);
      QRect lb_panel_lower(panel_x, panel_y, panel_w, panel_h);

      QRect lb_combined = lb_panel_upper.united(lb_panel_lower);
      bool hover_lb = lb_combined.contains(event->pos());

      get_rb_rq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                    &panel_w, &panel_h, false, 1.0);
      QRect rb_panel_upper(panel_x, panel_y, panel_w, panel_h);

      get_rb_rq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                    &panel_w, &panel_h, true, 1.0);
      QRect rb_panel_lower(panel_x, panel_y, panel_w, panel_h);

      QRect rb_combined = rb_panel_upper.united(rb_panel_lower);
      bool hover_rb = rb_combined.contains(event->pos());

      bool hover_changed = (g_ctrl.hover_lb_panel != hover_lb) ||
                           (g_ctrl.hover_rb_panel != hover_rb);
      g_ctrl.hover_lb_panel = hover_lb;
      g_ctrl.hover_rb_panel = hover_rb;

      if (hover_changed) {
        QRect bottom_right_rect(win_width / 2, win_height / 2,
                                win_width - win_width / 2,
                                win_height - win_height / 2);
        update(bottom_right_rect);
      }
    }
  }

  if (active_control_slider_ >= 0 && (event->buttons() & Qt::LeftButton)) {
    if (handle_control_slider_drag(active_control_slider_, event->pos().x(),
                                   width(), height())) {
      update(osm_panel_rect_);
      update(right_bottom_region());
    }
    event->accept();
    return;
  }

  if (scale_ruler_enabled_ && scale_ruler_has_start_ && !scale_ruler_end_fixed_ &&
      osm_panel_rect_.contains(event->pos())) {
    double lat = 0.0;
    double lon = 0.0;
    if (panel_point_to_llh(event->pos(), &lat, &lon)) {
      scale_ruler_has_end_ = true;
      scale_ruler_end_lat_deg_ = lat;
      scale_ruler_end_lon_deg_ = lon;
      update(osm_panel_rect_);
    }
  }

  if (map_osm_handle_move(
          event->pos(), event->buttons(), osm_panel_rect_,
      is_map_center_locked(), &dragging_osm_, &drag_moved_osm_,
          &drag_last_pos_, &osm_center_px_x_, &osm_center_px_y_,
          nullptr,
          [this]() {
            user_map_interacted_ = true;
            normalize_osm_center();
          },
          [this]() {
            const auto now_tp = std::chrono::steady_clock::now();
            if (last_tile_request_tp_ == std::chrono::steady_clock::time_point{} ||
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_tp - last_tile_request_tp_)
                        .count() >= tile_request_throttle_ms_) {
              last_tile_request_tp_ = now_tp;
              request_visible_tiles();
            }
          },
          [this]() {
            const auto now_tp = std::chrono::steady_clock::now();
            if (last_nfz_fetch_tp_ == std::chrono::steady_clock::time_point{} ||
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now_tp - last_nfz_fetch_tp_)
                        .count() >= nfz_fetch_throttle_ms_) {
              last_nfz_fetch_tp_ = now_tp;
              notify_nfz_viewport_changed();
            }
          },
          [this](const QRect &rect) { update(rect); })) {
    event->accept();
    return;
  }

  {
    bool running_ui = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
    }
    if (running_ui && !is_jam_map_locked() && osm_panel_rect_.contains(event->pos()) &&
        !(event->buttons() & Qt::LeftButton) && !dragging_osm_) {
      static std::chrono::steady_clock::time_point s_last_prefetch_tp{};
      static QPoint s_last_prefetch_pos(-100000, -100000);

      const auto now_tp = std::chrono::steady_clock::now();
      const bool time_ok =
          s_last_prefetch_tp == std::chrono::steady_clock::time_point{} ||
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now_tp - s_last_prefetch_tp)
              .count() >= 70;
      const bool move_ok =
          std::abs(event->pos().x() - s_last_prefetch_pos.x()) +
              std::abs(event->pos().y() - s_last_prefetch_pos.y()) >=
          10;

      if (time_ok && move_ok) {
        double lat = 0.0;
        double lon = 0.0;
        double slat = 0.0;
        double slon = 0.0;
        if (panel_point_to_llh(event->pos(), &lat, &lon) &&
            get_current_plan_anchor(&slat, &slon)) {
          const double hover_dist_m =
              distance_m_approx(slat, slon, lat, lon);
          if (hover_dist_m >= 8.0) {
            start_route_prefetch(slat, slon, lat, lon);
            s_last_prefetch_tp = now_tp;
            s_last_prefetch_pos = event->pos();
          }
        }
      }
    }
  }
  QWidget::mouseMoveEvent(event);
}

void MapWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    active_control_slider_ = -1;
    bool running_ui = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
    }

    if (scale_ruler_enabled_ && dragging_osm_ && !drag_moved_osm_ &&
        osm_panel_rect_.contains(event->pos())) {
      double lat = 0.0;
      double lon = 0.0;
      if (panel_point_to_llh(event->pos(), &lat, &lon)) {
        if (!scale_ruler_has_start_ || scale_ruler_end_fixed_) {
          scale_ruler_has_start_ = true;
          scale_ruler_start_lat_deg_ = lat;
          scale_ruler_start_lon_deg_ = lon;
          scale_ruler_has_end_ = false;
          scale_ruler_end_fixed_ = false;
        } else {
          scale_ruler_has_end_ = true;
          scale_ruler_end_lat_deg_ = lat;
          scale_ruler_end_lon_deg_ = lon;
          scale_ruler_end_fixed_ = true;
        }
        dragging_osm_ = false;
        update(osm_panel_rect_);
        event->accept();
        return;
      }
    }

    if (map_osm_handle_left_release(
            event->pos(), osm_panel_rect_, is_jam_map_locked(), running_ui,
            &suppress_left_click_release_, &dragging_osm_, &drag_moved_osm_,
            [this](const QPoint &pos) { set_preview_target(pos, PATH_MODE_LINE); },
            [this](const QPoint &pos) {
              if (!(dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled())) {
                return false;
              }
              double clk_lat = 0.0, clk_lon = 0.0;
              if (!panel_point_to_llh(pos, &clk_lat, &clk_lon)) {
                return false;
              }
              double target_lat = 0.0;
              double target_lon = 0.0;
              if (!nfz_pick_target_llh(dji_nfz_mgr_->get_zones(), clk_lat,
                                       clk_lon, &target_lat, &target_lon,
                                       nfz_layer_visible_.data())) {
                return false;
              }
              set_selected_llh_direct(target_lat, target_lon);
              notify_nfz_viewport_changed();
              return true;
            },
            [this](const QPoint &pos) { set_selected_llh_from_point(pos); },
            [this](const QRect &rect) { update(rect); })) {
      event->accept();
      return;
    }
    if (drag_moved_osm_) {
      request_visible_tiles();
      notify_nfz_viewport_changed();
      osm_bg_needs_redraw_ = true;
      update(osm_panel_rect_);
    }
    dragging_osm_ = false;
  }
  QWidget::mouseReleaseEvent(event);
}

void MapWidget::wheelEvent(QWheelEvent *event) {
  QPoint p0 = event->position().toPoint();

  if (tutorial_overlay_visible_) {
    const int delta = event->angleDelta().y();
    if (delta > 0) {
      tutorial_step_ = std::max(0, tutorial_step_ - 1);
    } else if (delta < 0) {
      tutorial_step_ = std::min(tutorial_last_step(), tutorial_step_ + 1);
    }
    tutorial_text_page_ = 0;
    tutorial_anim_step_anchor_ = -1;
    tutorial_has_glow_ = false;
    update();
    event->accept();
    return;
  }

  if (search_results_list_ && search_results_list_->isVisible() &&
      search_results_list_->geometry().contains(p0)) {
    QWidget::wheelEvent(event);
    return;
  }

  map_wheel_delta_accum_ += event->angleDelta().y();
  int wheel_steps = 0;
  while (map_wheel_delta_accum_ >= 120) {
    ++wheel_steps;
    map_wheel_delta_accum_ -= 120;
  }
  while (map_wheel_delta_accum_ <= -120) {
    --wheel_steps;
    map_wheel_delta_accum_ += 120;
  }
  if (wheel_steps == 0) {
    event->accept();
    return;
  }
  int delta = wheel_steps * 120;
  if (map_osm_handle_wheel(
      p0, delta, osm_panel_rect_, is_map_center_locked(), 19,
          &osm_zoom_,
          &osm_center_px_x_, &osm_center_px_y_,
          [this]() {
            user_map_interacted_ = true;
            normalize_osm_center();
          },
          [this]() { request_visible_tiles(); },
          [this]() { notify_nfz_viewport_changed(); },
          [this](const QRect &rect) { update(rect); })) {
    event->accept();
    return;
  }
  QWidget::wheelEvent(event);
}

void MapWidget::keyPressEvent(QKeyEvent *event) {
  if (tutorial_overlay_visible_) {
    if (event->key() == Qt::Key_Left) {
      tutorial_step_ = std::max(0, tutorial_step_ - 1);
      tutorial_text_page_ = 0;
      tutorial_anim_step_anchor_ = -1;
      tutorial_has_glow_ = false;
      update();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Right) {
      tutorial_step_ = std::min(tutorial_last_step(), tutorial_step_ + 1);
      tutorial_text_page_ = 0;
      tutorial_anim_step_anchor_ = -1;
      tutorial_has_glow_ = false;
      update();
      event->accept();
      return;
    }
  }
  QWidget::keyPressEvent(event);
}

void MapWidget::closeEvent(QCloseEvent *event) {
  commit_inline_edit(true);
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.running_ui = false;
  }
  g_runtime_abort = 1;
  g_gui_reset_waterfall_req.fetch_add(1);
  g_gui_stop_req.fetch_add(1);
  g_gui_exit_req.fetch_add(1);
  g_running.store(false);
  event->accept();
  QWidget::closeEvent(event);
}
#endif // MAIN_GUI_INPUT_EVENT_METHODS_INL_CONTEXT
