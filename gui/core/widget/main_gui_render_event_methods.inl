#ifndef MAIN_GUI_RENDER_EVENT_METHODS_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
void MapWidget::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  const QRegion dirty_region = event ? event->region() : QRegion(rect());
  p.setClipRegion(dirty_region);
  QLinearGradient scene_grad(rect().topLeft(), rect().bottomRight());
  scene_grad.setColorAt(0.0, QColor("#030712"));
  scene_grad.setColorAt(0.45, QColor("#0b1b31"));
  scene_grad.setColorAt(1.0, QColor("#05101f"));
  p.fillRect(rect(), scene_grad);

  auto dirty_intersects = [&](const QRect &r) {
    return dirty_region.intersects(r);
  };

  int win_width = width();
  int win_height = height();

  QRect osm_rect(0, 0, win_width / 2, win_height);
  if (dirty_intersects(osm_rect)) {
    draw_osm_panel(p, osm_rect);
  }

  QRect map_rect(win_width / 2, 0, win_width - win_width / 2,
                 win_height / 2);
  draw_map_panel(p, map_rect);

  bool running_ui = false;
  int interference_selection = -1;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    running_ui = g_ctrl.running_ui;
    interference_selection = g_ctrl.interference_selection;
  }
  const bool tx_active = g_tx_active.load();
  const bool crossbow_staging_panel =
      running_ui && (interference_selection == 2) && !tx_active;

  bool tutorial_waveform_preview =
      tutorial_overlay_visible_ && !running_ui &&
      (tutorial_step_ >= 9 && tutorial_step_ <= 12);

  QRect bottom_right_rect(win_width / 2, win_height / 2,
                          win_width - win_width / 2,
                          win_height - win_height / 2);

  // Keep hit-test rect in sync with what is currently shown.
  if (!crossbow_staging_panel) {
    crossbow_stage_launch_btn_rect_ = QRect();
    crossbow_whitelist_hit_rows_.clear();
    crossbow_whitelist_clear_btn_rect_ = QRect();
    crossbow_whitelist_unsync_btn_rect_ = QRect();
    crossbow_wifi_allow_apply_btn_rect_ = QRect();
    crossbow_bridge_mode_checkbox_rect_ = QRect();
    crossbow_blacklist_clear_btn_rect_ = QRect();
    crossbow_tab_original_rect_ = QRect();
    crossbow_tab_whitelist_rect_ = QRect();
    crossbow_tab_blacklist_rect_ = QRect();
    crossbow_tab_bridge_rect_ = QRect();
    crossbow_sort_btn_rect_ = QRect();
    crossbow_page_prev_btn_rect_ = QRect();
    crossbow_page_next_btn_rect_ = QRect();
    crossbow_stage_total_pages_ = 1;
    crossbow_stage_page_ = 0;
  }

  if (!running_ui && !tutorial_waveform_preview) {
    if (dirty_intersects(bottom_right_rect)) {
      draw_control_panel(p, win_width, win_height);
    }
  } else {
    if (dirty_intersects(bottom_right_rect)) {
      if (crossbow_staging_panel) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient panel_bg(bottom_right_rect.topLeft(), bottom_right_rect.bottomLeft());
        panel_bg.setColorAt(0.0, QColor("#0a1728"));
        panel_bg.setColorAt(1.0, QColor("#060f1d"));
        p.fillRect(bottom_right_rect, panel_bg);

        const QRect card = bottom_right_rect.adjusted(16, 16, -16, -16);
        p.setPen(QPen(QColor(125, 211, 252, 190), 1.2));
        p.setBrush(QColor(8, 18, 32, 228));
        p.drawRoundedRect(card, 10, 10);

        p.setPen(QColor("#7dd3fc"));
        QFont old_font = p.font();
        QFont title_font = old_font;
        title_font.setBold(true);
        p.setFont(title_font);
        const bool zh = gui_language_is_zh_tw(ui_language_);
        auto cbow_text = [&](const char *en, const char *zh_tw) {
          return zh ? QString::fromUtf8(zh_tw) : QString::fromLatin1(en);
        };
        auto normalize_token = [](const QString &value) {
          return value.trimmed().toLower().replace('-', ':');
        };
        p.drawText(QRect(card.x() + 12, card.y() + 8, card.width() - 24, 24),
             Qt::AlignLeft | Qt::AlignVCenter,
             cbow_text("Wireshark Live Capture / RID Stage",
                 "Wireshark 即時擷取 / RID 偵測階段"));
        crossbow_whitelist_hit_rows_.clear();
        crossbow_whitelist_clear_btn_rect_ = QRect();
        crossbow_whitelist_unsync_btn_rect_ = QRect();
        crossbow_wifi_allow_apply_btn_rect_ = QRect();
        crossbow_bridge_mode_checkbox_rect_ = QRect();
        crossbow_blacklist_clear_btn_rect_ = QRect();
        crossbow_tab_original_rect_ = QRect();
        crossbow_tab_whitelist_rect_ = QRect();
        crossbow_tab_blacklist_rect_ = QRect();
        crossbow_tab_bridge_rect_ = QRect();
        crossbow_sort_btn_rect_ = QRect();
        crossbow_page_prev_btn_rect_ = QRect();
        crossbow_page_next_btn_rect_ = QRect();

        struct StageRow {
          QString device_id;
          QString source;
          QString model;
          QString vendor;
          QString essid;
          QString security;
          bool rid = false;
          bool has_geo = false;
          double drone_lat = 0.0;
          double drone_lon = 0.0;
          bool has_bearing = false;
          double bearing_deg = 0.0;
          int channel = 0;
          quint64 beacon_count = 0;
          quint64 data_count = 0;
          double rssi_dbm = -999.0;
          bool has_rssi = false;
          bool whitelisted = false;
          bool blacklisted = false;
          bool confirmed_dji = false;
          bool special_rid = false;
          bool has_distance = false;
          double distance_m = 0.0;
          double confidence = 0.0;
          qint64 last_seen_ms = 0;
          double threat_score = 0.0;
        };
        struct DisplayRow {
          StageRow row;
          QString action_key;
          int action_kind = 0;
        };
        std::vector<StageRow> stage_rows;
        int whitelist_count = 0;
        int confirmed_count = 0;
        QStringList gui_whitelist_tokens;
        QStringList gui_blacklist_tokens;
        const QStringList special_rid_tokens = {
          QStringLiteral("90:3a:e6:38:bb:92"),
          QStringLiteral("e4:7a:2c:ef:68:17"),
        };
        auto normalize_special_token = [](const QString &value) {
          return value.trimmed().toLower().replace('-', ':');
        };
        auto is_special_rid_id = [&](const QString &device_id) {
          const QString norm_id = normalize_special_token(device_id);
          if (norm_id.isEmpty()) {
            return false;
          }
          for (const auto &token : special_rid_tokens) {
            if (norm_id.startsWith(normalize_special_token(token))) {
              return true;
            }
          }
          return false;
        };
        if (dji_detect_mgr_) {
          const auto snap = dji_detect_mgr_->targets_snapshot();
          stage_rows.reserve(snap.size());
          for (const auto &t : snap) {
            if (t.whitelisted) {
              whitelist_count++;
            } else if (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
                       t.source == QStringLiteral("bt-le-rid") ||
                       t.source == QStringLiteral("ble-rid") ||
                       t.source == QStringLiteral("wifi-nan-rid") ||
                       t.source == QStringLiteral("wifi-rid")) {
              confirmed_count++;
            }

            StageRow row;
            row.device_id = t.device_id.isEmpty() ? QStringLiteral("(unknown-id)") : t.device_id;
            row.source = t.source.isEmpty() ? QStringLiteral("unknown") : t.source;
            row.model = t.model.isEmpty() ? QStringLiteral("unknown") : t.model;
            row.vendor = t.vendor.isEmpty() ? QStringLiteral("unknown") : t.vendor;
            row.essid = t.essid;
            row.security = t.security;
            row.rid = (row.source == QStringLiteral("wifi-rid") ||
                       row.source == QStringLiteral("wifi-nan-rid") ||
                       row.source == QStringLiteral("bt-le-rid") ||
                       row.source == QStringLiteral("ble-rid"));
            row.has_geo = t.has_geo;
            row.drone_lat = t.drone_lat;
            row.drone_lon = t.drone_lon;
            row.has_bearing = t.has_bearing;
            row.bearing_deg = t.bearing_deg;
            row.channel = t.channel;
            row.beacon_count = t.beacon_count;
            row.data_count = t.data_count;
            row.rssi_dbm = t.rssi_dbm;
            row.has_rssi = t.has_rssi;
            row.whitelisted = t.whitelisted;
            row.confirmed_dji = t.confirmed_dji;
            row.special_rid = is_special_rid_id(row.device_id);
            row.has_distance = t.has_distance;
            row.distance_m = t.distance_m;
            row.confidence = t.confidence;
            row.last_seen_ms = t.last_seen_ms;

            if (!row.whitelisted) {
              if (row.confirmed_dji) {
                row.threat_score += 100.0;
              }
              if (row.source == QStringLiteral("aoa-passive")) {
                row.threat_score += 90.0;
              } else if (row.source == QStringLiteral("wifi-nan-rid")) {
                row.threat_score += 78.0;
              } else if (row.source == QStringLiteral("wifi-rid")) {
                row.threat_score += 75.0;
              } else if (row.source == QStringLiteral("bt-le-rid") ||
                         row.source == QStringLiteral("ble-rid")) {
                row.threat_score += 65.0;
              }
              row.threat_score += std::max(0.0, std::min(30.0, row.confidence * 30.0));
              if (row.has_distance && row.distance_m > 0.0) {
                row.threat_score += std::max(0.0, 40.0 - std::min(40.0, row.distance_m));
              }
            }
            stage_rows.push_back(row);
          }

          const auto wl_items = dji_detect_mgr_->whitelist_items();
          if (!wl_items.empty()) {
            QStringList wl_tokens;
            wl_tokens.reserve((int)wl_items.size());
            for (const auto &it : wl_items) {
              if (!it.trimmed().isEmpty()) {
                wl_tokens.push_back(it.trimmed());
              }
            }
            if (!wl_tokens.isEmpty()) {
              gui_whitelist_tokens = wl_tokens;
            }
          }

          const auto bl_items = dji_detect_mgr_->blacklist_items();
          if (!bl_items.empty()) {
            QStringList bl_tokens;
            bl_tokens.reserve((int)bl_items.size());
            for (const auto &it : bl_items) {
              if (!it.trimmed().isEmpty()) {
                bl_tokens.push_back(it.trimmed());
              }
            }
            if (!bl_tokens.isEmpty()) {
              gui_blacklist_tokens = bl_tokens;
            }
          }
        }

        auto token_matches_row = [&](const QString &token, const StageRow &row) {
          const QString norm_token = normalize_token(token);
          const QString norm_id = normalize_token(row.device_id);
          if (norm_token.isEmpty() || norm_id.isEmpty()) {
            return false;
          }
          return norm_id.startsWith(norm_token);
        };
        auto is_pinned_rid_row = [&](const StageRow &row) {
          return row.rid && row.special_rid;
        };

        for (auto &row : stage_rows) {
          for (const auto &token : gui_blacklist_tokens) {
            if (token_matches_row(token, row)) {
              row.blacklisted = true;
              break;
            }
          }
        }

        /* crossbow_view_mode_ persists across paint events; set by tab clicks */
        QString allow_csv;
        bool bridge_state_known = false;
        {
          std::lock_guard<std::mutex> lk(g_gui_wifi_rid_applied_mtx);
          bridge_state_known = g_gui_wifi_rid_applied_initialized;
          if (bridge_state_known) {
            allow_csv = QString::fromStdString(g_gui_wifi_rid_applied_csv).trimmed();
          }
        }
        if (!bridge_state_known) {
          const char *allow_env = std::getenv("BDS_WIFI_RID_ALLOW_IDS");
          allow_csv = QString::fromUtf8(allow_env ? allow_env : "").trimmed();
        }
        const QStringList bridge_filter_tokens = allow_csv.split(',', Qt::SkipEmptyParts);

        bool bridge_mode_known = false;
        bool bridge_mode_mixed = true;
        {
          std::lock_guard<std::mutex> lk(g_gui_wifi_rid_mode_applied_mtx);
          bridge_mode_known = g_gui_wifi_rid_mode_applied_initialized;
          if (bridge_mode_known) {
            bridge_mode_mixed = g_gui_wifi_rid_mode_mixed_applied;
          }
        }
        if (!bridge_mode_known) {
          const char *mode_env = std::getenv("BDS_WIFI_RID_WIFI_MODE");
          if (mode_env && mode_env[0] != '\0') {
            QString mode = QString::fromUtf8(mode_env).trimmed().toLower();
            bridge_mode_mixed = (mode == QStringLiteral("mixed"));
            bridge_mode_known = true;
          }
        }
        if (!bridge_mode_known) {
          const char *mixed_env = std::getenv("BDS_WIFI_RID_MIXED_ENABLE");
          if (mixed_env && mixed_env[0] != '\0') {
            QString flag = QString::fromUtf8(mixed_env).trimmed().toLower();
            bridge_mode_mixed = (flag == QStringLiteral("1") ||
                                 flag == QStringLiteral("true") ||
                                 flag == QStringLiteral("yes") ||
                                 flag == QStringLiteral("on"));
          }
        }

        const int filter_bar_bottom_y = card.y() + 34;

        /* --- Live Capture single tab --- */
        const int wtab_y = filter_bar_bottom_y + 5;
        const int wtab_h = 26;
        {
          crossbow_view_mode_ = 0;
          crossbow_tab_whitelist_rect_ = QRect();
          crossbow_tab_blacklist_rect_ = QRect();
          crossbow_tab_bridge_rect_ = QRect();
          const QRect tr(card.x() + 12, wtab_y, card.width() - 24, wtab_h);
          p.setPen(QPen(QColor(56, 189, 248, 230), 1.2));
          p.setBrush(QColor(30, 64, 175, 210));
          p.drawRoundedRect(tr, 6, 6);
          p.setPen(QColor(229, 231, 235));
          p.setFont(old_font);
          p.drawText(tr, Qt::AlignCenter,
                     zh ? QString::fromUtf8("\xe5\x8d\xb3\xe6\x99\x82\xe6\x93\x8a\xe5\x8f\x96")
                        : QStringLiteral("Live Capture"));
          crossbow_tab_original_rect_ = tr;
        }

        /* LAUNCH button geometry — defined here so all tab branches can use it */
        const int launch_btn_h = 38;
        const int launch_btn_w = 150;
        const int launch_btn_x = card.center().x() - launch_btn_w / 2;
        const int launch_btn_y = card.bottom() - launch_btn_h - 14;
        crossbow_stage_launch_btn_rect_ = QRect(launch_btn_x, launch_btn_y,
                                                launch_btn_w, launch_btn_h);

        if (crossbow_view_mode_ == 0) { /* ====== Live Capture tab ====== */
        if (crossbow_stage_sort_mode_ == 0) {
          std::stable_sort(stage_rows.begin(), stage_rows.end(),
                           [](const StageRow &a, const StageRow &b) {
                             if (a.has_rssi != b.has_rssi) {
                               return a.has_rssi && !b.has_rssi;
                             }
                             if (a.has_rssi && b.has_rssi && std::fabs(a.rssi_dbm - b.rssi_dbm) > 1e-6) {
                               return a.rssi_dbm > b.rssi_dbm;
                             }
                             if (a.has_distance != b.has_distance) {
                               return a.has_distance && !b.has_distance;
                             }
                             if (a.has_distance && b.has_distance &&
                                 std::fabs(a.distance_m - b.distance_m) > 1e-6) {
                               return a.distance_m < b.distance_m;
                             }
                             return a.last_seen_ms > b.last_seen_ms;
                           });
        } else if (crossbow_stage_sort_mode_ == 1) {
          std::stable_sort(stage_rows.begin(), stage_rows.end(),
                           [](const StageRow &a, const StageRow &b) {
                             if (std::fabs(a.threat_score - b.threat_score) > 1e-6) {
                               return a.threat_score > b.threat_score;
                             }
                             return a.last_seen_ms > b.last_seen_ms;
                           });
        } else {
          std::stable_sort(stage_rows.begin(), stage_rows.end(),
                           [](const StageRow &a, const StageRow &b) {
                             return a.last_seen_ms > b.last_seen_ms;
                           });
        }
        std::stable_partition(stage_rows.begin(), stage_rows.end(),
                              [&](const StageRow &row) {
                                return is_pinned_rid_row(row);
                              });

        p.setFont(old_font);
        p.setPen(QColor("#cbd5e1"));
        int ly = wtab_y + wtab_h + 8;
        int display_packet_count = 0;
        int display_rid_count = 0;
        for (const auto &row : stage_rows) {
          if (row.rid && !row.has_geo) continue;
          if (row.source == QStringLiteral("aoa-passive")) continue;
          if (!bridge_mode_mixed && row.source == QStringLiteral("wifi-beacon")) continue;
          display_packet_count++;
          if (row.rid) display_rid_count++;
        }
        const int summary_h = 24;
        const QRect summary_rect(card.x() + 14, ly, card.width() - 28, summary_h);
        p.drawText(summary_rect, Qt::AlignLeft | Qt::AlignVCenter,
                   zh ? QString::fromUtf8("封包: %1  已解析 RID: %2").arg(display_packet_count).arg(display_rid_count)
                      : QString("Packets: %1  RID decoded: %2").arg(display_packet_count).arg(display_rid_count));

        ly += summary_h + 8;

        // ====== WiFi Mixed Mode Checkbox ======
        const int cb_h = 28;
        const int cb_w = std::max(180, card.width() - 28);
        crossbow_bridge_mode_checkbox_rect_ = QRect(card.x() + 14, ly, cb_w, cb_h);
        p.setPen(QPen(QColor(71, 85, 105, 210), 1.0));
        p.setBrush(QColor(12, 23, 38, 190));
        p.drawRoundedRect(crossbow_bridge_mode_checkbox_rect_, 7, 7);

        const QRect box_rect(crossbow_bridge_mode_checkbox_rect_.x() + 10,
                             crossbow_bridge_mode_checkbox_rect_.y() + 5,
                             18,
                             18);
        p.setPen(QPen(bridge_mode_mixed ? QColor(56, 189, 248, 230)
                                        : QColor(100, 116, 139, 210),
                      1.2));
        p.setBrush(bridge_mode_mixed ? QColor(30, 64, 175, 215)
                                     : QColor(15, 23, 42, 210));
        p.drawRoundedRect(box_rect, 4, 4);
        if (bridge_mode_mixed) {
          p.setPen(QPen(QColor("#e0f2fe"), 2.0));
          p.drawLine(box_rect.x() + 4, box_rect.y() + 10,
                     box_rect.x() + 8, box_rect.y() + 14);
          p.drawLine(box_rect.x() + 8, box_rect.y() + 14,
                     box_rect.x() + 14, box_rect.y() + 5);
        }

        p.setPen(QColor("#e2e8f0"));
        p.drawText(QRect(box_rect.right() + 10,
                         crossbow_bridge_mode_checkbox_rect_.y(),
                         crossbow_bridge_mode_checkbox_rect_.width() - 38,
                         cb_h),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   bridge_mode_mixed
                 ? cbow_text("Mixed scan (single-NIC compatibility)", "混合掃描（單卡相容）")
                 : cbow_text("RID-only (single-NIC compatibility)", "僅 RID（單卡相容）"));

        ly += cb_h + 8;

        const QRect table_rect(card.x() + 12, ly, card.width() - 24, std::max(110, card.height() - 250));
        p.setPen(QPen(QColor(71, 85, 105, 210), 1.0));
        p.setBrush(QColor(12, 23, 38, 180));
        p.drawRoundedRect(table_rect, 8, 8);

        std::vector<DisplayRow> display_rows;
        display_rows.reserve(stage_rows.size());
        for (const auto &row : stage_rows) {
          // Strict RID rule: hide RID rows that do not carry valid geo coordinates.
          if (row.rid && !row.has_geo) {
            continue;
          }
          // No coarse-estimation targets in Live Capture list.
          if (row.source == QStringLiteral("aoa-passive")) {
            continue;
          }
          // Filter based on WiFi mode: if RID-only, hide WiFi beacons
          if (!bridge_mode_mixed && row.source == QStringLiteral("wifi-beacon")) {
            continue;  // Skip WiFi beacons in RID-only mode
          }
          DisplayRow display;
          display.row = row;
          display.action_key = row.device_id;
          display.action_kind = 0;
          display_rows.push_back(display);
        }

        const QString sort_label =
          (crossbow_stage_sort_mode_ == 0)
            ? cbow_text("Sort: Signal", "排序: 訊號")
            : ((crossbow_stage_sort_mode_ == 1)
                 ? cbow_text("Sort: Threat", "排序: 威脅")
                 : cbow_text("Sort: Latest", "排序: 最新"));
        const int sort_btn_w = 132;
        const int sort_btn_h = 24;

        const int row_h = 24;
        const int col_bssid = (int)std::lround(table_rect.width() * 0.15);
        const int col_pwr = (int)std::lround(table_rect.width() * 0.08);
        const int col_bcn = (int)std::lround(table_rect.width() * 0.10);
        const int col_data = (int)std::lround(table_rect.width() * 0.10);
        const int col_ch = (int)std::lround(table_rect.width() * 0.07);
        const int col_sec = (int)std::lround(table_rect.width() * 0.12);
        const int col_rid = 42;
        const int col_essid = table_rect.width() - col_bssid - col_pwr - col_bcn - col_data - col_ch - col_sec - col_rid;
        const int pager_btn_w = 58;
        const int pager_btn_h = 24;
        const int footer_h = pager_btn_h + 6;
        const QRect head_rect(table_rect.x() + 8, table_rect.y() + 6, table_rect.width() - 16, row_h);
        const int body_top = head_rect.bottom() + 6;
        const int body_bottom = table_rect.bottom() - footer_h - 6;
        const int body_h = std::max(row_h, body_bottom - body_top + 1);
        int row_y = body_top;
        const int rows_per_page = std::max(1, body_h / row_h);
        QFont head_font = old_font;
        head_font.setBold(true);
        p.setPen(QColor("#93c5fd"));
        p.setFont(head_font);
        p.drawText(QRect(head_rect.x(), head_rect.y(), col_bssid - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "BSSID");
        p.drawText(QRect(head_rect.x() + col_bssid, head_rect.y(), col_pwr - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "PWR");
        p.drawText(QRect(head_rect.x() + col_bssid + col_pwr, head_rect.y(), col_bcn - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "BCN");
        p.drawText(QRect(head_rect.x() + col_bssid + col_pwr + col_bcn, head_rect.y(), col_data - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "DATA");
        p.drawText(QRect(head_rect.x() + col_bssid + col_pwr + col_bcn + col_data, head_rect.y(), col_ch - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "CH");
        p.drawText(QRect(head_rect.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch, head_rect.y(), col_sec - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "ENC");
          p.drawText(QRect(head_rect.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch + col_sec, head_rect.y(), col_rid - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "RID");
          p.drawText(QRect(head_rect.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch + col_sec + col_rid, head_rect.y(), col_essid - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "ESSID");

        p.setFont(old_font);
        crossbow_stage_total_pages_ =
            std::max(1, ((int)display_rows.size() + rows_per_page - 1) / rows_per_page);
        if (crossbow_stage_page_ < 0) {
          crossbow_stage_page_ = 0;
        }
        if (crossbow_stage_page_ >= crossbow_stage_total_pages_) {
          crossbow_stage_page_ = crossbow_stage_total_pages_ - 1;
        }
        const int start_idx = crossbow_stage_page_ * rows_per_page;
        const int end_idx = std::min((int)display_rows.size(), start_idx + rows_per_page);
        const int visible_rows = std::max(0, end_idx - start_idx);
        for (int i = 0; i < visible_rows; ++i) {
          const DisplayRow &display = display_rows[(size_t)(start_idx + i)];
          const StageRow &row = display.row;
          const QRect rr(head_rect.x(), row_y, head_rect.width(), row_h);
          if ((i % 2) == 0) {
            p.fillRect(rr, QColor(15, 30, 50, 96));
          }
          if (row.special_rid) {
            p.fillRect(QRect(rr.x(), rr.y() + 3, 4, row_h - 6), QColor(251, 191, 36, 230));
          }

          p.setPen(row.special_rid ? QColor(253, 224, 71) : QColor("#e2e8f0"));
          p.drawText(QRect(rr.x() + 2, rr.y(), col_bssid - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(row.device_id, Qt::ElideMiddle, col_bssid - 12));

          p.setPen(row.has_rssi ? QColor(110, 231, 183) : QColor("#94a3b8"));
          const QString pwr_text = row.has_rssi ? QString::number((int)std::lround(row.rssi_dbm)) : QStringLiteral("n/a");
          p.drawText(QRect(rr.x() + col_bssid, rr.y(), col_pwr - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter, pwr_text);

          p.setPen(QColor("#cbd5e1"));
          p.drawText(QRect(rr.x() + col_bssid + col_pwr, rr.y(), col_bcn - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     row.beacon_count > 0 ? QString::number((qulonglong)row.beacon_count) : QStringLiteral("-"));
          p.drawText(QRect(rr.x() + col_bssid + col_pwr + col_bcn, rr.y(), col_data - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     row.data_count > 0 ? QString::number((qulonglong)row.data_count) : QStringLiteral("-"));

          p.setPen(QColor("#fde68a"));
          p.drawText(QRect(rr.x() + col_bssid + col_pwr + col_bcn + col_data, rr.y(), col_ch - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     row.channel > 0 ? QString::number(row.channel) : QStringLiteral("-"));

          p.setPen(QColor("#93c5fd"));
          QString enc_text = row.security.isEmpty() ? row.source : row.security;
          if (row.source == QStringLiteral("wifi-rid") &&
              row.model.compare(QStringLiteral("wireshark"), Qt::CaseInsensitive) == 0) {
            enc_text = QStringLiteral("wireshark");
          }
          p.drawText(QRect(rr.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch, rr.y(), col_sec - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(enc_text, Qt::ElideRight, col_sec - 12));

              p.setPen(row.special_rid ? QColor(253, 224, 71)
                                       : (row.rid ? QColor(110, 231, 183) : QColor("#94a3b8")));
              p.drawText(QRect(rr.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch + col_sec,
                   rr.y(), col_rid - 8, row_h),
                 Qt::AlignLeft | Qt::AlignVCenter,
                 row.rid ? (row.special_rid ? QStringLiteral("Y*") : QStringLiteral("Y"))
                         : QStringLiteral("-"));

          p.setPen(QColor("#f8fafc"));
              QString essid_text = row.essid.isEmpty() ? row.vendor : row.essid;
              if (row.rid) {
                if (row.has_geo) {
                  essid_text = QString("lat %1 lon %2")
                                   .arg(row.drone_lat, 0, 'f', 6)
                                   .arg(row.drone_lon, 0, 'f', 6);
                }
              }
              p.drawText(QRect(rr.x() + col_bssid + col_pwr + col_bcn + col_data + col_ch + col_sec + col_rid,
                           rr.y(), col_essid - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(essid_text, Qt::ElideRight, col_essid - 12));

          row_y += row_h;
        }
        if (visible_rows == 0) {
          p.setPen(QColor("#94a3b8"));
          p.drawText(QRect(head_rect.x(), body_top, head_rect.width(), row_h),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     cbow_text("No packets available", "目前沒有可顯示封包"));
        }

        const int pager_y = table_rect.bottom() - pager_btn_h - 6;
        const int page_text_w = 140;
        const int side_gap = 10;
        QRect page_left_rect(table_rect.x() + 8, pager_y, sort_btn_w, sort_btn_h);
        crossbow_sort_btn_rect_ = page_left_rect;
        const QRect page_text_rect(table_rect.right() - page_text_w - 8,
                                   pager_y,
                                   page_text_w,
                                   pager_btn_h);
        const int center_group_w = pager_btn_w * 2 + 8;
        int center_group_x = table_rect.center().x() - (center_group_w / 2);
        const int min_center_group_x = page_left_rect.right() + side_gap;
        const int max_center_group_x = page_text_rect.x() - side_gap - center_group_w;
        if (center_group_x < min_center_group_x) {
          center_group_x = min_center_group_x;
        }
        if (center_group_x > max_center_group_x) {
          center_group_x = max_center_group_x;
        }
        crossbow_page_prev_btn_rect_ = QRect(center_group_x,
                                             pager_y,
                                             pager_btn_w,
                                             pager_btn_h);
        crossbow_page_next_btn_rect_ = QRect(crossbow_page_prev_btn_rect_.right() + 8,
                                             pager_y,
                                             pager_btn_w,
                                             pager_btn_h);
        p.setPen(QPen(QColor(148, 163, 184, 210), 1.0));
        p.setBrush(QColor(30, 41, 59, 210));
        p.drawRoundedRect(page_left_rect, 6, 6);
        p.drawRoundedRect(crossbow_page_prev_btn_rect_, 6, 6);
        p.drawRoundedRect(crossbow_page_next_btn_rect_, 6, 6);
        p.setPen(QColor("#e2e8f0"));
          p.drawText(page_left_rect, Qt::AlignCenter, sort_label);
          p.drawText(crossbow_page_prev_btn_rect_, Qt::AlignCenter, cbow_text("Prev", "上一頁"));
          p.drawText(crossbow_page_next_btn_rect_, Qt::AlignCenter, cbow_text("Next", "下一頁"));
        p.drawText(page_text_rect,
                   Qt::AlignRight | Qt::AlignVCenter,
             zh ? QString::fromUtf8("頁面 %1/%2")
                  .arg(crossbow_stage_page_ + 1)
                  .arg(crossbow_stage_total_pages_)
                : QString("Page %1/%2")
                  .arg(crossbow_stage_page_ + 1)
                  .arg(crossbow_stage_total_pages_));

        crossbow_whitelist_unsync_btn_rect_ = QRect();
        crossbow_wifi_allow_apply_btn_rect_ = QRect();

        } /* ====== end Live Capture tab ====== */

        /* ====== Whitelist tab (mode 1) ====== */
        else if (crossbow_view_mode_ == 1) {
          const int lx = card.x() + 14;
          const int lw = card.width() - 28;
          const int content_bot = crossbow_stage_launch_btn_rect_.y() - 10;
          const QRect wl_card(lx, wtab_y + wtab_h + 8, lw,
                              std::max(60, content_bot - (wtab_y + wtab_h + 8)));
          p.setPen(QPen(QColor(71, 85, 105, 210), 1.0));
          p.setBrush(QColor(12, 23, 38, 180));
          p.drawRoundedRect(wl_card, 8, 8);
          { QFont hf = old_font; hf.setBold(true); p.setFont(hf); }
          p.setPen(QColor("#93c5fd"));
          p.drawText(QRect(wl_card.x() + 10, wl_card.y() + 6, lw - 20, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     zh ? QString::fromUtf8("\xe5\x85\x81\xe8\xa8\xb1\xe6\xb8\x85\xe5\x96\xae (Display Filter)")
                        : "Allow List (Display Filter)");
          p.setFont(old_font);
          int iy = wl_card.y() + 30;
          const int ih = 22;
          if (gui_whitelist_tokens.isEmpty()) {
            p.setPen(QColor("#64748b"));
            p.drawText(QRect(wl_card.x() + 10, iy, lw - 20, ih),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       zh ? QString::fromUtf8("(\xe7\xa9\xba\xe7\x99\xbd)") : "(empty)");
          } else {
            for (int i = 0; i < gui_whitelist_tokens.size() &&
                           iy + ih <= wl_card.bottom() - 36; ++i) {
              if (i % 2 == 0)
                p.fillRect(QRect(wl_card.x() + 4, iy, lw - 8, ih), QColor(15, 30, 50, 96));
              p.setPen(QColor("#e2e8f0"));
              p.drawText(QRect(wl_card.x() + 10, iy, lw - 20, ih),
                         Qt::AlignLeft | Qt::AlignVCenter, gui_whitelist_tokens[i]);
              iy += ih;
            }
          }
          const int clr_w = 160, clr_h = 28;
          crossbow_whitelist_clear_btn_rect_ = QRect(
              wl_card.x() + (lw - clr_w) / 2,
              wl_card.bottom() - clr_h - 6,
              clr_w, clr_h);
          p.setPen(QPen(QColor(248, 113, 113, 220), 1.0));
          p.setBrush(QColor(127, 29, 29, 220));
          p.drawRoundedRect(crossbow_whitelist_clear_btn_rect_, 6, 6);
          p.setPen(QColor("#fee2e2"));
          p.drawText(crossbow_whitelist_clear_btn_rect_, Qt::AlignCenter,
                     zh ? QString::fromUtf8("\xe6\xb8\x85\xe9\x99\xa4\xe5\x85\x81\xe8\xa8\xb1\xe6\xb8\x85\xe5\x96\xae")
                        : "Clear Allow List");
        }

        /* ====== Blacklist tab (mode 2) ====== */
        else if (crossbow_view_mode_ == 2) {
          const int lx = card.x() + 14;
          const int lw = card.width() - 28;
          const int content_bot = crossbow_stage_launch_btn_rect_.y() - 10;
          const QRect bl_card(lx, wtab_y + wtab_h + 8, lw,
                              std::max(60, content_bot - (wtab_y + wtab_h + 8)));
          p.setPen(QPen(QColor(71, 85, 105, 210), 1.0));
          p.setBrush(QColor(12, 23, 38, 180));
          p.drawRoundedRect(bl_card, 8, 8);
          { QFont hf = old_font; hf.setBold(true); p.setFont(hf); }
          p.setPen(QColor("#f87171"));
          p.drawText(QRect(bl_card.x() + 10, bl_card.y() + 6, lw - 20, 20),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     zh ? QString::fromUtf8("\xe5\xb0\x81\xe9\x8e\x96\xe6\xb8\x85\xe5\x96\xae (Block List)")
                        : "Block List");
          p.setFont(old_font);
          int iy = bl_card.y() + 30;
          const int ih = 22;
          if (gui_blacklist_tokens.isEmpty()) {
            p.setPen(QColor("#64748b"));
            p.drawText(QRect(bl_card.x() + 10, iy, lw - 20, ih),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       zh ? QString::fromUtf8("(\xe7\xa9\xba\xe7\x99\xbd)") : "(empty)");
          } else {
            for (int i = 0; i < gui_blacklist_tokens.size() &&
                           iy + ih <= bl_card.bottom() - 36; ++i) {
              if (i % 2 == 0)
                p.fillRect(QRect(bl_card.x() + 4, iy, lw - 8, ih), QColor(40, 10, 10, 96));
              p.setPen(QColor("#fca5a5"));
              p.drawText(QRect(bl_card.x() + 10, iy, lw - 20, ih),
                         Qt::AlignLeft | Qt::AlignVCenter, gui_blacklist_tokens[i]);
              iy += ih;
            }
          }
          const int clr_w = 160, clr_h = 28;
          crossbow_blacklist_clear_btn_rect_ = QRect(
              bl_card.x() + (lw - clr_w) / 2,
              bl_card.bottom() - clr_h - 6,
              clr_w, clr_h);
          p.setPen(QPen(QColor(248, 113, 113, 220), 1.0));
          p.setBrush(QColor(127, 29, 29, 220));
          p.drawRoundedRect(crossbow_blacklist_clear_btn_rect_, 6, 6);
          p.setPen(QColor("#fee2e2"));
          p.drawText(crossbow_blacklist_clear_btn_rect_, Qt::AlignCenter,
                     zh ? QString::fromUtf8("\xe6\xb8\x85\xe9\x99\xa4\xe5\xb0\x81\xe9\x8e\x96\xe6\xb8\x85\xe5\x96\xae")
                        : "Clear Block List");
        }

        /* ====== Bridge Status tab (mode 3) ====== */
        else if (crossbow_view_mode_ == 3) {
          int bly = wtab_y + wtab_h + 8;
          if (dji_detect_mgr_) {
            const auto id_lock3 = dji_detect_mgr_->last_rid_identity_lock_snapshot();
            const long long now_ms3 = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch()).count();
            const long long lock_age3 = id_lock3.has_lock
                ? std::max(0LL, now_ms3 - (long long)id_lock3.last_seen_ms) : 0LL;
            const bool lock_stale3 = id_lock3.has_lock && lock_age3 > 10000;
            const int lh3 = 70;
            const QRect lr3(card.x() + 14, bly, card.width() - 28, lh3);
            p.setPen(QPen(lock_stale3 ? QColor(251, 146, 60, 205) : QColor(96, 165, 250, 195), 1.0));
            p.setBrush(QColor(10, 24, 38, 212));
            p.drawRoundedRect(lr3, 8, 8);
            { QFont tf3 = old_font; tf3.setBold(true); p.setFont(tf3); }
            p.setPen(QColor("#e2e8f0"));
            p.drawText(QRect(lr3.x() + 10, lr3.y() + 6, lr3.width() - 20, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       cbow_text("RID Identity Lock", "RID \xe8\xba\xab\xe5\x88\x86\xe9\x8e\x96\xe5\xae\x9a"));
            p.setFont(old_font);
            QString ll1_3, ll2_3;
            if (id_lock3.has_lock) {
              ll1_3 = QString("id %1  |  mac %2")
                          .arg(id_lock3.remote_id_id.isEmpty() ? QStringLiteral("(n/a)") : id_lock3.remote_id_id,
                               id_lock3.mac.isEmpty()          ? QStringLiteral("(n/a)") : id_lock3.mac);
              ll2_3 = QString("lat %1 lon %2  |  %3  |  %4")
                          .arg(id_lock3.drone_lat, 0, 'f', 7)
                          .arg(id_lock3.drone_lon, 0, 'f', 7)
                          .arg(lock_stale3
                                   ? QString("stale %1s").arg(lock_age3 / 1000.0, 0, 'f', 1)
                                   : QString("age %1s").arg(lock_age3 / 1000.0, 0, 'f', 1))
                          .arg(id_lock3.protocol.isEmpty() ? QStringLiteral("RID") : id_lock3.protocol);
            } else {
              ll1_3 = cbow_text("waiting for valid RID coordinates", "\xe7\xad\x89\xe5\xbe\x85\xe6\x9c\x89\xe6\x95\x88 RID \xe5\xba\xa7\xe6\xa8\x99");
              ll2_3 = cbow_text("needs remote_id/mac + drone_lat/lon", "\xe9\x9c\x80\xe8\xa6\x81 remote_id/mac + drone_lat/lon");
            }
            p.setPen(lock_stale3 ? QColor(254, 215, 170) : QColor(147, 197, 253, 225));
            p.drawText(QRect(lr3.x() + 10, lr3.y() + 26, lr3.width() - 20, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(ll1_3, Qt::ElideRight, lr3.width() - 20));
            p.setPen(QColor("#cbd5e1"));
            p.drawText(QRect(lr3.x() + 10, lr3.y() + 44, lr3.width() - 20, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(ll2_3, Qt::ElideRight, lr3.width() - 20));
            bly += lh3 + 10;

            const auto rt3 = dji_detect_mgr_->last_rid_runtime_snapshot();
            struct BCard3 { QString title; DjiRidBridgeSnapshot snap; };
            const BCard3 bcards3[] = {
                { QStringLiteral("Wi-Fi RID"), rt3.wifi },
                { QStringLiteral("BLE RID"),   rt3.ble  },
            };
            const int bcgap3 = 10;
            const int bch3   = 82;
            const int bcw3   = (card.width() - 28 - bcgap3) / 2;
            for (int bi = 0; bi < 2; ++bi) {
              const QRect &br3 = (bi == 0)
                  ? QRect(card.x() + 14, bly, bcw3, bch3)
                  : QRect(card.x() + 14 + bcw3 + bcgap3, bly, card.width() - 28 - bcw3 - bcgap3, bch3);
              const DjiRidBridgeSnapshot &sn3 = bcards3[bi].snap;
              const bool hs3 = sn3.has_sample;
              const long long age3 = hs3
                  ? std::max(0LL, now_ms3 - (long long)sn3.last_update_ms) : 0LL;
              const bool stale3   = hs3 && age3 > 4000;
              const bool warning3 = stale3 ||
                                    sn3.health_status == QStringLiteral("warn-idle") ||
                                    sn3.health_status == QStringLiteral("idle") ||
                                    sn3.health_status == QStringLiteral("warn-no-signature");
              p.setPen(QPen(warning3 ? QColor(248, 113, 113, 200) : QColor(45, 212, 191, 180), 1.0));
              p.setBrush(QColor(8, 20, 32, 210));
              p.drawRoundedRect(br3, 8, 8);
              const QString sl3 = hs3
                  ? QString("%1 | hit %2% | %3")
                        .arg(sn3.health_status.isEmpty() ? QStringLiteral("ok") : sn3.health_status)
                        .arg(sn3.hit_rate, 0, 'f', 1)
                        .arg(stale3 ? QString("stale %1s").arg(age3 / 1000.0, 0, 'f', 1)
                                    : QString("age %1s").arg(age3 / 1000.0, 0, 'f', 1))
                  : cbow_text("waiting for bridge telemetry", "\xe7\xad\x89\xe5\xbe\x85 Bridge \xe9\x81\x99\xe6\xb8\xac");
              const QString stats3 = hs3
                  ? QString("Rx %1  OUI %2  ODID %3  Loc %4  Drop %5")
                        .arg((qulonglong)sn3.rx).arg((qulonglong)sn3.oui)
                        .arg((qulonglong)sn3.odid).arg((qulonglong)sn3.loc)
                        .arg((qulonglong)sn3.dropped)
                  : QStringLiteral("-");
              QString rt3l = hs3
                  ? QString("Rec %1  Idle %2  NoSig %3")
                        .arg(sn3.recovery_count).arg(sn3.idle_windows).arg(sn3.no_signature_windows)
                  : QStringLiteral("-");
              if (hs3 && !sn3.iface_or_hci.isEmpty()) rt3l += QString("  %1").arg(sn3.iface_or_hci);
              if (hs3 && sn3.bridge.contains(QStringLiteral("tshark"), Qt::CaseInsensitive))
                rt3l += QString("  engine:wireshark");
              QString det3 = hs3 ? sn3.health_detail : QString();
              if (hs3 && !sn3.profile.isEmpty())    det3 = det3.isEmpty() ? QString("profile %1").arg(sn3.profile)    : QString("%1 | %2").arg(sn3.profile, det3);
              if (hs3 && !sn3.self_check.isEmpty()) det3 = det3.isEmpty() ? QString("self-check %1").arg(sn3.self_check) : QString("%1 | self-check %2").arg(det3, sn3.self_check);
              if (hs3 && !sn3.bridge.isEmpty())     det3 = det3.isEmpty() ? QString("bridge %1").arg(sn3.bridge)     : QString("%1 | bridge %2").arg(det3, sn3.bridge);
              { QFont tf3 = old_font; tf3.setBold(true); p.setFont(tf3); }
              p.setPen(QColor("#e2e8f0"));
              p.drawText(QRect(br3.x() + 10, br3.y() + 6, br3.width() - 20, 18),
                         Qt::AlignLeft | Qt::AlignVCenter, bcards3[bi].title);
              p.setFont(old_font);
              p.setPen(warning3 ? QColor(254, 202, 202) : QColor(167, 243, 208));
              p.drawText(QRect(br3.x() + 10, br3.y() + 24, br3.width() - 20, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         p.fontMetrics().elidedText(sl3, Qt::ElideRight, br3.width() - 20));
              p.setPen(QColor("#bfdbfe"));
              p.drawText(QRect(br3.x() + 10, br3.y() + 40, br3.width() - 20, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         p.fontMetrics().elidedText(stats3, Qt::ElideRight, br3.width() - 20));
              p.setPen(QColor("#cbd5e1"));
              p.drawText(QRect(br3.x() + 10, br3.y() + 56, br3.width() - 20, 16),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         p.fontMetrics().elidedText(rt3l, Qt::ElideRight, br3.width() - 20));
              if (!det3.isEmpty()) {
                p.setPen(QColor("#94a3b8"));
                p.drawText(QRect(br3.x() + 10, br3.bottom() - 18, br3.width() - 20, 14),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           p.fontMetrics().elidedText(det3, Qt::ElideRight, br3.width() - 20));
              }
            }
            bly += bch3 + 10;

            /* ── Radio monitor strip (Wi-Fi + BLE) ─────────────────── */
            const auto &wifi_nics = rt3.all_wifi;
            const auto &ble_nics = rt3.all_ble;
            const int row_count = wifi_nics.size() + ble_nics.size();
            if (row_count > 0) {
              const int row_h = 18;
              const int strip_pad = 5;
              const int strip_h = strip_pad * 2 + row_h * row_count + 16;
              const QRect strip_rect(card.x() + 14, bly,
                                     card.width() - 28, strip_h);
              p.setPen(QPen(QColor(100, 180, 255, 120), 1.0));
              p.setBrush(QColor(6, 16, 30, 200));
              p.drawRoundedRect(strip_rect, 6, 6);
              { QFont tf_mon = old_font; tf_mon.setBold(true);
                p.setFont(tf_mon); p.setPen(QColor("#93c5fd")); }
              p.drawText(QRect(strip_rect.x() + 8, strip_rect.y() + 3,
                               strip_rect.width() - 16, 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Radio Monitor (Wi-Fi + BLE)"));
              p.setFont(old_font);

              int row_y = strip_rect.y() + strip_pad + 16;
              const long long now_mon_ms = QDateTime::currentMSecsSinceEpoch();
              const int half = strip_rect.width() / 2;

              for (auto it = wifi_nics.cbegin(); it != wifi_nics.cend();
                   ++it, row_y += row_h) {
                const DjiRidBridgeSnapshot &sn = it.value();
                const long long age_ms =
                    std::max(0LL, now_mon_ms - (long long)sn.last_update_ms);
                const int alpha = (age_ms > 4000) ? 90 : 220;
                const QString iface_short = sn.iface_or_hci.length() > 13
                    ? QString("...") + sn.iface_or_hci.right(12)
                    : sn.iface_or_hci;
                const QString role_tag = sn.role.isEmpty()
                    ? QStringLiteral("wifi") : sn.role;
                const QColor role_col =
                    (sn.role == QStringLiteral("tracker"))  ? QColor(251,191,36,alpha)
                  : (sn.role == QStringLiteral("searcher")) ? QColor(74,222,128,alpha)
                  :                                           QColor(148,163,184,alpha);
                const QString ch_str = (sn.channel > 0)
                    ? QString("Wi-Fi ch %1").arg(sn.channel)
                    : QStringLiteral("Wi-Fi ch --");
                const QColor ch_col =
                    (sn.role == QStringLiteral("tracker") && sn.channel > 0)
                        ? QColor(253,224,71,alpha) : QColor(186,230,253,alpha);
                p.setPen(QColor(203,213,225,alpha));
                p.drawText(QRect(strip_rect.x()+8, row_y, half-8, row_h),
                           Qt::AlignLeft|Qt::AlignVCenter, iface_short);
                p.setPen(role_col);
                p.drawText(QRect(strip_rect.x()+half, row_y, 64, row_h),
                           Qt::AlignLeft|Qt::AlignVCenter, role_tag);
                p.setPen(ch_col);
                { QFont tf_ch = old_font;
                  tf_ch.setBold(sn.role == QStringLiteral("tracker"));
                  p.setFont(tf_ch); }
                p.drawText(QRect(strip_rect.x()+half+64, row_y,
                                 strip_rect.right()-8-(strip_rect.x()+half+64), row_h),
                           Qt::AlignLeft|Qt::AlignVCenter, ch_str);
                p.setFont(old_font);
              }

              for (auto it = ble_nics.cbegin(); it != ble_nics.cend();
                   ++it, row_y += row_h) {
                const DjiRidBridgeSnapshot &sn = it.value();
                const long long age_ms =
                    std::max(0LL, now_mon_ms - (long long)sn.last_update_ms);
                const int alpha = (age_ms > 4000) ? 90 : 220;
                const QString dev = sn.iface_or_hci.isEmpty() ? it.key() : sn.iface_or_hci;
                QString ble_mode = sn.profile;
                if (ble_mode.isEmpty()) ble_mode = QStringLiteral("unknown");
                const QString mode_l = ble_mode.toLower();
                if (mode_l.contains(QStringLiteral("legacy"))) {
                  ble_mode = QStringLiteral("Legacy 4.x");
                } else if (mode_l.contains(QStringLiteral("long")) ||
                           mode_l.contains(QStringLiteral("lr"))) {
                  ble_mode = QStringLiteral("Long Range 5.x");
                }
                p.setPen(QColor(203,213,225,alpha));
                p.drawText(QRect(strip_rect.x()+8, row_y, half-8, row_h),
                           Qt::AlignLeft|Qt::AlignVCenter, dev);
                p.setPen(QColor(244,114,182,alpha));
                p.drawText(QRect(strip_rect.x()+half, row_y, 64, row_h),
                           Qt::AlignLeft|Qt::AlignVCenter, QStringLiteral("ble"));
                p.setPen(QColor(196,181,253,alpha));
                p.drawText(QRect(strip_rect.x()+half+64, row_y,
                                 strip_rect.right()-8-(strip_rect.x()+half+64), row_h),
                           Qt::AlignLeft|Qt::AlignVCenter,
                           QString("BLE %1").arg(ble_mode));
              }
              bly += strip_h + 6;
            }

          }
        }

        /* ====== LAUNCH button — always shown on all tabs ====== */
        p.setPen(QPen(QColor(56, 189, 248, 230), 1.2));
        p.setBrush(QColor(30, 64, 175, 210));
        p.drawRoundedRect(crossbow_stage_launch_btn_rect_, 10, 10);
        p.setPen(QColor("#e5e7eb"));
        QFont btn_font = old_font;
        btn_font.setBold(true);
        p.setFont(btn_font);
        p.drawText(crossbow_stage_launch_btn_rect_, Qt::AlignCenter,
             cbow_text("LAUNCH", "\xe5\x95\x9f\xe5\x8b\x95"));

        p.restore();
        return;
      }

      MapMonitorPanelsInput in;
      {
        std::lock_guard<std::mutex> lk(g_ctrl_mtx);
        in.ctrl = g_ctrl;
      }
      in.spec_snap = spec_snap_;
      in.waterfall_image = wf_.image;
      in.language = ui_language_;
      // Radar replacement is active only while Crossbow runtime is active.
      const bool is_running_crossbow =
          (in.ctrl.interference_selection == 2) && in.ctrl.running_ui;
      in.show_drone_center = is_running_crossbow;
      if (dji_detect_mgr_ && is_running_crossbow) {
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
            if (t.has_geo && g_receiver_valid) {
              used_distance = distance_m_approx(g_receiver_lat_deg, g_receiver_lon_deg,
                                                t.drone_lat, t.drone_lon);
              used_bearing = bearing_from_to_deg(g_receiver_lat_deg, g_receiver_lon_deg,
                                                 t.drone_lat, t.drone_lon);
              used_has_bearing = std::isfinite(used_bearing);
              used_has_distance = std::isfinite(used_distance) && used_distance > 0.0;
            }
            const bool drawable_target =
                !t.whitelisted && used_has_bearing && used_has_distance && used_distance > 0.0 &&
                (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
                 t.source == QStringLiteral("bt-le-rid") ||
               t.source == QStringLiteral("ble-rid") ||
                 t.source == QStringLiteral("wifi-nan-rid") ||
                 t.source == QStringLiteral("wifi-rid"));
            if (drawable_target) {
              DroneCenterRadarPoint rp;
              rp.bearing_deg = used_bearing;
              rp.distance_m = used_distance;
              rp.speed_mps = t.speed_mps;
              rp.has_speed = t.has_velocity;
              rp.hostile =
                  (t.confirmed_dji || t.source == QStringLiteral("aoa-passive"));
              rp.is_ble_rid = (t.source == QStringLiteral("bt-le-rid") ||
                               t.source == QStringLiteral("ble-rid"));
                rp.is_wifi_rid = (t.source == QStringLiteral("wifi-rid") ||
                        t.source == QStringLiteral("wifi-nan-rid"));
              if ((int)in.radar_points.size() < 12) {
                in.radar_points.push_back(rp);
              }
            }
          }
        }
      map_draw_spectrum_panel(p, win_width, win_height, in);
      map_draw_waterfall_panel(p, win_width, win_height, in);
      map_draw_time_panel(p, win_width, win_height, in);
      map_draw_constellation_panel(p, win_width, win_height, in);
    }
  }

  if (tutorial_overlay_visible_ || dirty_intersects(bottom_right_rect) ||
      dirty_intersects(osm_rect) || dirty_intersects(map_rect)) {
    draw_tutorial_overlay(p, win_width, win_height);
  }
}

void MapWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  layout_overlay_widgets(width(), height());
  if (alert_overlay_) {
    alert_overlay_->setGeometry(rect());
  }
  update_alert_overlay();
  map_panel_bootstrap_redraw_done_ = false;
  update(QRect(width() / 2, 0, width() - width() / 2, height() / 2));
}
#endif // MAIN_GUI_RENDER_EVENT_METHODS_INL_CONTEXT
