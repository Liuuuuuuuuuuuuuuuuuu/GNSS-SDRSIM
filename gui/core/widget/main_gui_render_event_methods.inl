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
        p.drawText(QRect(card.x() + 12, card.y() + 8, card.width() - 24, 24),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   "Crossbow Settings / Detect Stage");
        crossbow_whitelist_hit_rows_.clear();
        crossbow_whitelist_clear_btn_rect_ = QRect();
        crossbow_sort_btn_rect_ = QRect();
        crossbow_page_prev_btn_rect_ = QRect();
        crossbow_page_next_btn_rect_ = QRect();

        struct StageRow {
          QString device_id;
          QString source;
          QString vendor;
          bool whitelisted = false;
          bool confirmed_dji = false;
          bool has_distance = false;
          double distance_m = 0.0;
          double confidence = 0.0;
          qint64 last_seen_ms = 0;
          double threat_score = 0.0;
        };
        std::vector<StageRow> stage_rows;
        int unknown_count = 0;
        int whitelist_count = 0;
        int confirmed_count = 0;
        if (dji_detect_mgr_) {
          const auto snap = dji_detect_mgr_->targets_snapshot();
          stage_rows.reserve(snap.size());
          for (const auto &t : snap) {
            if (t.whitelisted) {
              whitelist_count++;
            } else if (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
                       t.source == QStringLiteral("bt-le-rid") ||
                       t.source == QStringLiteral("wifi-nan-rid") ||
                       t.source == QStringLiteral("wifi-rid")) {
              confirmed_count++;
            } else {
              unknown_count++;
            }

            StageRow row;
            row.device_id = t.device_id.isEmpty() ? QStringLiteral("(unknown-id)") : t.device_id;
            row.source = t.source.isEmpty() ? QStringLiteral("unknown") : t.source;
            row.vendor = t.vendor.isEmpty() ? QStringLiteral("unknown") : t.vendor;
            row.whitelisted = t.whitelisted;
            row.confirmed_dji = t.confirmed_dji;
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
              } else if (row.source == QStringLiteral("bt-le-rid")) {
                row.threat_score += 65.0;
              }
              row.threat_score += std::max(0.0, std::min(30.0, row.confidence * 30.0));
              if (row.has_distance && row.distance_m > 0.0) {
                row.threat_score += std::max(0.0, 40.0 - std::min(40.0, row.distance_m));
              }
            }
            stage_rows.push_back(row);
          }
        }

        if (crossbow_stage_sort_mode_ == 0) {
          std::stable_sort(stage_rows.begin(), stage_rows.end(),
                           [](const StageRow &a, const StageRow &b) {
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

        p.setFont(old_font);
        p.setPen(QColor("#cbd5e1"));
        const int lx = card.x() + 14;
        int ly = card.y() + 40;
        const int line_h = 24;
        p.drawText(QRect(lx, ly, card.width() - 28, line_h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("Unknown signals: %1").arg(unknown_count));
        ly += line_h;
        p.drawText(QRect(lx, ly, card.width() - 28, line_h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("Whitelist: %1").arg(whitelist_count));
        ly += line_h;
        p.drawText(QRect(lx, ly, card.width() - 28, line_h), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("Confirmed targets: %1").arg(confirmed_count));
        ly += line_h + 8;

        const QRect table_rect(card.x() + 12, ly, card.width() - 24, std::max(110, card.height() - 220));
        p.setPen(QPen(QColor(71, 85, 105, 210), 1.0));
        p.setBrush(QColor(12, 23, 38, 180));
        p.drawRoundedRect(table_rect, 8, 8);

        const QString sort_label =
          (crossbow_stage_sort_mode_ == 0)
            ? QStringLiteral("Sort: Distance")
            : ((crossbow_stage_sort_mode_ == 1)
                 ? QStringLiteral("Sort: Threat")
                 : QStringLiteral("Sort: Latest"));
        const int sort_btn_w = 132;
        const int sort_btn_h = 24;
        crossbow_sort_btn_rect_ = QRect(table_rect.right() - sort_btn_w - 8,
                        table_rect.y() + 6,
                        sort_btn_w,
                        sort_btn_h);
        p.setPen(QPen(QColor(125, 211, 252, 210), 1.0));
        p.setBrush(QColor(30, 41, 59, 220));
        p.drawRoundedRect(crossbow_sort_btn_rect_, 6, 6);
        p.setPen(QColor("#e2e8f0"));
        p.drawText(crossbow_sort_btn_rect_, Qt::AlignCenter, sort_label);

        const int row_h = 24;
        const int col_id = (int)std::lround(table_rect.width() * 0.33);
        const int col_source = (int)std::lround(table_rect.width() * 0.20);
        const int col_vendor = (int)std::lround(table_rect.width() * 0.21);
        const int col_action = table_rect.width() - col_id - col_source - col_vendor;
        const QRect head_rect(table_rect.x() + 8, table_rect.y() + 34, table_rect.width() - 16, row_h);

        p.setPen(QColor("#93c5fd"));
        QFont head_font = old_font;
        head_font.setBold(true);
        p.setFont(head_font);
        p.drawText(QRect(head_rect.x(), head_rect.y(), col_id - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "Signal ID");
        p.drawText(QRect(head_rect.x() + col_id, head_rect.y(), col_source - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "Source");
        p.drawText(QRect(head_rect.x() + col_id + col_source, head_rect.y(), col_vendor - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "Vendor");
        p.drawText(QRect(head_rect.x() + col_id + col_source + col_vendor, head_rect.y(), col_action - 6, row_h), Qt::AlignLeft | Qt::AlignVCenter, "Whitelist");

        p.setFont(old_font);
        int row_y = head_rect.bottom() + 4;
        const int max_rows = std::max(1, (table_rect.height() - row_h - 12) / row_h);
        int rows_per_page = std::max(1, max_rows - 1);
        crossbow_stage_total_pages_ =
            std::max(1, ((int)stage_rows.size() + rows_per_page - 1) / rows_per_page);
        if (crossbow_stage_page_ < 0) {
          crossbow_stage_page_ = 0;
        }
        if (crossbow_stage_page_ >= crossbow_stage_total_pages_) {
          crossbow_stage_page_ = crossbow_stage_total_pages_ - 1;
        }

        const int start_idx = crossbow_stage_page_ * rows_per_page;
        const int end_idx = std::min((int)stage_rows.size(), start_idx + rows_per_page);
        const int visible_rows = std::max(0, end_idx - start_idx);
        for (int i = 0; i < visible_rows; ++i) {
          const StageRow &row = stage_rows[(size_t)(start_idx + i)];
          const QRect rr(head_rect.x(), row_y, head_rect.width(), row_h);
          if ((i % 2) == 0) {
            p.fillRect(rr, QColor(15, 30, 50, 96));
          }

          p.setPen(QColor("#e2e8f0"));
          p.drawText(QRect(rr.x() + 2, rr.y(), col_id - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(row.device_id, Qt::ElideMiddle, col_id - 12));

          QColor src_col = QColor("#cbd5e1");
          if (row.source == QStringLiteral("wifi-rid")) {
            src_col = QColor(248, 113, 113);
          } else if (row.source == QStringLiteral("wifi-nan-rid")) {
            src_col = QColor(45, 212, 191);
          } else if (row.source == QStringLiteral("bt-le-rid")) {
            src_col = QColor(96, 165, 250);
          } else if (row.source == QStringLiteral("aoa-passive")) {
            src_col = QColor(253, 224, 71);
          }
          p.setPen(src_col);
          p.drawText(QRect(rr.x() + col_id, rr.y(), col_source - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(row.source, Qt::ElideRight, col_source - 12));

          p.setPen(QColor("#a7f3d0"));
          p.drawText(QRect(rr.x() + col_id + col_source, rr.y(), col_vendor - 8, row_h), Qt::AlignLeft | Qt::AlignVCenter,
                     p.fontMetrics().elidedText(row.vendor, Qt::ElideRight, col_vendor - 12));

          const QRect action_btn(rr.x() + col_id + col_source + col_vendor + 4,
                                 rr.y() + 2, col_action - 10, row_h - 4);
          const QColor action_bg = row.whitelisted ? QColor(30, 58, 138, 220)
                                                   : QColor(6, 95, 70, 220);
          const QColor action_bd = row.whitelisted ? QColor(147, 197, 253)
                                                   : QColor(110, 231, 183);
          p.setPen(QPen(action_bd, 1.0));
          p.setBrush(action_bg);
          p.drawRoundedRect(action_btn, 6, 6);
          p.setPen(QColor("#f8fafc"));
          p.drawText(action_btn, Qt::AlignCenter,
                     row.whitelisted ? QStringLiteral("Remove") : QStringLiteral("Add"));

          CrossbowWhitelistHit hit;
          hit.btn_rect = action_btn;
          hit.device_id = row.device_id;
          hit.currently_whitelisted = row.whitelisted;
          crossbow_whitelist_hit_rows_.push_back(hit);

          row_y += row_h;
        }

        const int pager_btn_w = 58;
        const int pager_btn_h = 24;
        crossbow_page_prev_btn_rect_ = QRect(table_rect.x() + 8,
                                             table_rect.bottom() - pager_btn_h - 6,
                                             pager_btn_w,
                                             pager_btn_h);
        crossbow_page_next_btn_rect_ = QRect(crossbow_page_prev_btn_rect_.right() + 6,
                                             crossbow_page_prev_btn_rect_.y(),
                                             pager_btn_w,
                                             pager_btn_h);
        p.setPen(QPen(QColor(148, 163, 184, 210), 1.0));
        p.setBrush(QColor(30, 41, 59, 210));
        p.drawRoundedRect(crossbow_page_prev_btn_rect_, 6, 6);
        p.drawRoundedRect(crossbow_page_next_btn_rect_, 6, 6);
        p.setPen(QColor("#e2e8f0"));
        p.drawText(crossbow_page_prev_btn_rect_, Qt::AlignCenter, "Prev");
        p.drawText(crossbow_page_next_btn_rect_, Qt::AlignCenter, "Next");
        p.drawText(QRect(crossbow_page_next_btn_rect_.right() + 10,
                         crossbow_page_next_btn_rect_.y(),
                         160,
                         pager_btn_h),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString("Page %1/%2")
                       .arg(crossbow_stage_page_ + 1)
                       .arg(crossbow_stage_total_pages_));

        const int launch_area_h = 86;
        const int clear_btn_w = 160;
        const int clear_btn_h = 30;
        const int clear_btn_x = card.x() + 14;
        const int clear_btn_y = card.bottom() - launch_area_h + 2;
        crossbow_whitelist_clear_btn_rect_ = QRect(clear_btn_x, clear_btn_y, clear_btn_w, clear_btn_h);
        p.setPen(QPen(QColor(148, 163, 184, 210), 1.0));
        p.setBrush(QColor(30, 41, 59, 210));
        p.drawRoundedRect(crossbow_whitelist_clear_btn_rect_, 7, 7);
        p.setPen(QColor("#e2e8f0"));
        p.drawText(crossbow_whitelist_clear_btn_rect_, Qt::AlignCenter, "Clear Whitelist");

        // Launch button: positioned near the bottom in START-like action area.
        const int btn_w = 150;
        const int btn_h = 38;
        const int btn_x = card.center().x() - btn_w / 2;
        const int btn_y = card.bottom() - btn_h - 24;
        crossbow_stage_launch_btn_rect_ = QRect(btn_x, btn_y, btn_w, btn_h);

        p.setPen(QPen(QColor(56, 189, 248, 230), 1.2));
        p.setBrush(QColor(30, 64, 175, 210));
        p.drawRoundedRect(crossbow_stage_launch_btn_rect_, 10, 10);
        p.setPen(QColor("#e5e7eb"));
        QFont btn_font = old_font;
        btn_font.setBold(true);
        p.setFont(btn_font);
        p.drawText(crossbow_stage_launch_btn_rect_, Qt::AlignCenter, "LAUNCH");

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
        // Extract radar points for Crossbow mode display
        const bool is_cbow = (in.ctrl.interference_selection == 2);
        in.show_drone_center = is_cbow && in.ctrl.show_detailed_ctrl;
        if (dji_detect_mgr_ && is_cbow) {
          const auto snap = dji_detect_mgr_->targets_snapshot();
          for (const auto &t : snap) {
            const bool drawable_target =
                !t.whitelisted && t.has_bearing && t.has_distance && t.distance_m > 0.0 &&
                (t.confirmed_dji || t.source == QStringLiteral("aoa-passive") ||
                 t.source == QStringLiteral("bt-le-rid") ||
                 t.source == QStringLiteral("wifi-nan-rid") ||
                 t.source == QStringLiteral("wifi-rid"));
            if (drawable_target) {
              DroneCenterRadarPoint rp;
              rp.bearing_deg = t.bearing_deg;
              rp.distance_m = t.distance_m;
              rp.speed_mps = t.speed_mps;
              rp.has_speed = t.has_velocity;
              rp.hostile =
                  (t.confirmed_dji || t.source == QStringLiteral("aoa-passive"));
              rp.is_ble_rid = (t.source == QStringLiteral("bt-le-rid"));
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