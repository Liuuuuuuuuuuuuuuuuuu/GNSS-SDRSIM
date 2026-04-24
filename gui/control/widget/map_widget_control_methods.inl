#ifndef MAP_WIDGET_CONTROL_METHODS_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
void MapWidget::refresh_tick_timer() {
  if (!timer_)
    return;

  int interval_ms = 16;
  if (!isVisible() || isMinimized()) {
    interval_ms = 500;
  } else {
    bool running_ui = false;
    int interference_selection = 0;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
      interference_selection = g_ctrl.interference_selection;
    }

    bool startup_boost = false;
    {
      std::lock_guard<std::mutex> lk(g_time_mtx);
      const auto now = std::chrono::steady_clock::now();
      const auto since_start =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_tp)
              .count();
      const auto since_tx =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - g_tx_start_tp)
              .count();
      startup_boost = (since_start >= 0 && since_start < 5000) ||
                      (g_tx_active.load() && since_tx >= 0 && since_tx < 2500);
    }

    const bool map_busy = dragging_osm_ || !tile_pending_.empty();

    if (tutorial_overlay_visible_) {
      // Keep tutorial animation smooth at baseline 60Hz cadence.
      interval_ms = 16;
    } else if (startup_boost || map_busy) {
      // Startup/tile fill keeps the same 16ms cadence for consistent sweep timing.
      interval_ms = 16;
    } else if (running_ui && interference_selection == 2) {
      // Crossbow radar mode: 16ms cadence (~60Hz).
      interval_ms = 16;
    } else if (running_ui) {
      // Active runtime view: 16ms cadence (~60Hz).
      interval_ms = 16;
    } else {
      // Idle view keeps the same baseline cadence.
      interval_ms = 16;
    }
  }

  if (!timer_->isActive() || timer_->interval() != interval_ms) {
    timer_->start(interval_ms);
  }
}

void MapWidget::draw_control_panel(QPainter &p, int win_width, int win_height) {
  MapControlPanelInput in;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    in.ctrl = g_ctrl;
  }
  in.time_info = time_info_;
  in.language = ui_language_;
  in.control_text_scale = control_text_scale_;
  in.caption_text_scale = control_caption_scale_;
  in.switch_option_text_scale = control_switch_option_scale_;
  in.value_text_scale = control_value_scale_;
  in.accent_color = control_accent_color_;
  in.border_color = control_border_color_;
  in.text_color = control_text_color_;
  in.dim_text_color = control_dim_text_color_;
  in.layout_overrides = &control_layout_overrides_;
  in.slider_part_overrides = &control_slider_part_overrides_;
    in.rnx_name_bds = QString::fromUtf8(
      in.ctrl.rinex_name_bds[0] ? in.ctrl.rinex_name_bds : "N/A");
    in.rnx_name_gps = QString::fromUtf8(
      in.ctrl.rinex_name_gps[0] ? in.ctrl.rinex_name_gps : "N/A");

    for (const auto &sat : sats_) {
      const bool is_active =
          (sat.prn >= 1 && sat.prn < MAX_SAT && g_active_prn_mask[sat.prn] != 0);
      if (!is_active)
        continue;
      if (sat.is_gps)
        in.detail_active_gps_prns.push_back(sat.prn);
      else
        in.detail_active_bds_prns.push_back(sat.prn);
    }
    std::sort(in.detail_active_gps_prns.begin(), in.detail_active_gps_prns.end());
    in.detail_active_gps_prns.erase(
        std::unique(in.detail_active_gps_prns.begin(), in.detail_active_gps_prns.end()),
        in.detail_active_gps_prns.end());
    std::sort(in.detail_active_bds_prns.begin(), in.detail_active_bds_prns.end());
    in.detail_active_bds_prns.erase(
        std::unique(in.detail_active_bds_prns.begin(), in.detail_active_bds_prns.end()),
        in.detail_active_bds_prns.end());

    // Counter-UAV control-center data: show only in crossbow/jam-auto mode.
    in.show_drone_center = false;
    if (dji_detect_mgr_) {
      const auto snap = dji_detect_mgr_->targets_snapshot();
      for (const auto &t : snap) {
        DroneCenterListItem item;
        item.device_id = t.device_id;
        item.model = t.model.isEmpty() ? QStringLiteral("Unknown") : t.model.toUpper();
        item.confidence = t.confidence;

        if (t.whitelisted) {
          in.whitelist_count += 1;
          if ((int)in.whitelist_items.size() < 3) {
            in.whitelist_items.push_back(item);
          }
        } else if (t.confirmed_dji || t.source == QStringLiteral("bt-le-rid") ||
             t.source == QStringLiteral("ble-rid") ||
                   t.source == QStringLiteral("wifi-nan-rid") ||
                   t.source == QStringLiteral("wifi-rid") ||
                   t.source == QStringLiteral("aoa-passive")) {
          in.confirmed_drone_count += 1;
          if ((int)in.confirmed_items.size() < 3) {
            in.confirmed_items.push_back(item);
          }
          if (t.has_bearing && t.has_distance) {
            DroneCenterRadarPoint rp;
            rp.bearing_deg = t.bearing_deg;
            rp.distance_m = t.distance_m;
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
        } else {
          in.unknown_signal_count += 1;
          if ((int)in.unknown_items.size() < 3) {
            in.unknown_items.push_back(item);
          }
        }
      }
    }

  ControlLayout lo;
  compute_control_layout(win_width, win_height, &lo, in.ctrl.show_detailed_ctrl,
                         in.ctrl.signal_mode != SIG_MODE_MIXED);
  control_gear_btn_rect_ =
      QRect(lo.header_gear.x, lo.header_gear.y, lo.header_gear.w, lo.header_gear.h);

  map_draw_control_panel(p, win_width, win_height, in);
  for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
    const ControlElementAppearanceOverride &ov =
        control_element_appearance_overrides_[i];
    if (!ov.enabled) {
      continue;
    }
    const Rect *rr = control_layout_rect(&lo, (ControlLayoutElementId)i);
    if (!rr || rr->w <= 0 || rr->h <= 0) {
      continue;
    }
    MapControlPanelInput scoped_in = in;
    apply_element_appearance_override(&scoped_in, ov);
    p.save();
    p.setClipRect(QRect(rr->x, rr->y, rr->w, rr->h));
    map_draw_control_panel(p, win_width, win_height, scoped_in);
    p.restore();
  }
}

bool MapWidget::request_crossbow_unlock_with_sudo_prompt() {
  bool already_unlocked = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    already_unlocked = g_ctrl.crossbow_unlocked;
  }
  if (already_unlocked) {
    return true;
  }

  bool ok = false;
  const QString title = gui_i18n_text(ui_language_, "label.crossbow");
  const QString prompt = gui_language_is_zh_tw(ui_language_)
                             ? QString::fromUtf8("輸入 sudo 密碼以啟用 CROSSBOW（並啟動 WiFi/BLE 偵測）")
                             : QString("Enter sudo password to unlock CROSSBOW (enable WiFi/BLE detection)");
  const QString password = QInputDialog::getText(
      this, title, prompt, QLineEdit::Password, QString(), &ok);
  if (!ok) {
    return false;
  }
  if (password.isEmpty()) {
    map_gui_push_alert(1, gui_language_is_zh_tw(ui_language_)
                             ? "未輸入密碼，未啟用 CROSSBOW"
                             : "No password entered. CROSSBOW remains locked.");
    return false;
  }

  QProcess proc;
  QStringList args;
  args << "-S" << "-v";
  proc.start(QStringLiteral("sudo"), args);
  if (!proc.waitForStarted(3000)) {
    map_gui_push_alert(2, gui_language_is_zh_tw(ui_language_)
                             ? "無法啟動 sudo 驗證程序"
                             : "Failed to start sudo validation process.");
    return false;
  }
  QByteArray input = password.toUtf8();
  input.push_back('\n');
  proc.write(input);
  proc.closeWriteChannel();
  if (!proc.waitForFinished(5000) || proc.exitStatus() != QProcess::NormalExit ||
      proc.exitCode() != 0) {
    map_gui_push_alert(2, gui_language_is_zh_tw(ui_language_)
                             ? "sudo 密碼驗證失敗，CROSSBOW 未啟用"
                             : "sudo authentication failed. CROSSBOW remains locked.");
    return false;
  }

  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.crossbow_unlocked = true;
  }
  g_gui_crossbow_unlock_req.fetch_add(1);
  map_gui_push_alert(0, gui_language_is_zh_tw(ui_language_)
                           ? "CROSSBOW 已解鎖，WiFi/BLE 偵測已啟用"
                           : "CROSSBOW unlocked. WiFi/BLE detection enabled.");
  return true;
}

void MapWidget::on_control_gear_click() {
  const auto now = std::chrono::steady_clock::now();
  const bool has_last_tap =
      (control_gear_tap_last_tp_ != std::chrono::steady_clock::time_point{});
  const bool continue_sequence =
      has_last_tap &&
      (std::chrono::duration_cast<std::chrono::milliseconds>(
           now - control_gear_tap_last_tp_)
         .count() <= 300);

  if (!continue_sequence) {
    control_gear_tap_count_ = 0;
  }

  control_gear_tap_count_ += 1;
  control_gear_tap_last_tp_ = now;

  if (control_gear_tap_count_ == 6) {
    map_gui_push_alert(
        0,
        gui_language_is_zh_tw(ui_language_)
            ? "偵測功能準備中，再連點 4 下齒輪可啟用 CROSSBOW"
            : "Detection prep ready. Tap gear 4 more times to unlock CROSSBOW");
  }

  if (control_gear_tap_count_ >= 10) {
    if (control_gear_click_timer_) {
      control_gear_click_timer_->stop();
    }
    control_gear_tap_count_ = 0;
    control_gear_tap_last_tp_ = std::chrono::steady_clock::time_point{};
    request_crossbow_unlock_with_sudo_prompt();
    return;
  }

  if (control_gear_click_timer_) {
    control_gear_click_timer_->start(300);
  }
}

void MapWidget::open_control_style_dialog() {
  const double original_control_text_scale = control_text_scale_;
  const double original_caption_scale = control_caption_scale_;
  const double original_switch_option_scale = control_switch_option_scale_;
  const double original_value_scale = control_value_scale_;
  const QColor original_accent = control_accent_color_;
  const QColor original_border = control_border_color_;
  const QColor original_text = control_text_color_;
  const QColor original_dim = control_dim_text_color_;
  ControlElementAppearanceOverride
      original_element_appearance_overrides[CTRL_LAYOUT_ELEMENT_COUNT];
  for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
    original_element_appearance_overrides[i] =
        control_element_appearance_overrides_[i];
  }
  const ControlLayoutOverrides original_layout_overrides = control_layout_overrides_;
  const ControlSliderPartOverrides original_slider_part_overrides =
      control_slider_part_overrides_;
  const bool original_font_zh_kai = font_zh_kai_;
  GuiControlState preview_ctrl{};
  bool crossbow_unlocked = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    preview_ctrl = g_ctrl;
    crossbow_unlocked = g_ctrl.crossbow_unlocked;
  }
  const auto tr = [&](const char *key) { return gui_i18n_text(ui_language_, key); };

  QDialog dlg(this);
  dlg.setWindowTitle(tr("style.dialog.title"));
  dlg.setModal(true);
  const int panel_preview_w = std::max(520, width() / 2);
  const int panel_preview_h = std::max(330, height() / 2);
  const int sidebar_w = 500;
  QSize target_size(panel_preview_w + sidebar_w + 48, panel_preview_h + 48);
  if (window() && window()->windowHandle() && window()->windowHandle()->screen()) {
    const QRect available = window()->windowHandle()->screen()->availableGeometry();
    target_size.setWidth(std::min(target_size.width(), available.width() - 24));
    target_size.setHeight(std::min(target_size.height(), available.height() - 24));
  }
  dlg.resize(target_size);
  dlg.setMinimumSize(target_size);
  dlg.setMaximumSize(target_size);
  dlg.setSizeGripEnabled(false);

  auto *root = new QVBoxLayout(&dlg);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  ControlLayoutOverrides preview_layout_overrides = control_layout_overrides_;
  ControlSliderPartOverrides preview_slider_part_overrides =
      control_slider_part_overrides_;
  ControlElementAppearanceOverride
      preview_element_appearance_overrides[CTRL_LAYOUT_ELEMENT_COUNT];
  for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
    preview_element_appearance_overrides[i] =
        control_element_appearance_overrides_[i];
  }
  auto sanitize_position_offsets = [&]() {
    for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
      preview_layout_overrides.entries[i].dx = 0;
      preview_layout_overrides.entries[i].dy = 0;
      preview_slider_part_overrides.label[i].dx = 0;
      preview_slider_part_overrides.label[i].dy = 0;
      preview_slider_part_overrides.track[i].dx = 0;
      preview_slider_part_overrides.track[i].dy = 0;
      preview_slider_part_overrides.value[i].dx = 0;
      preview_slider_part_overrides.value[i].dy = 0;
    }
  };
  sanitize_position_offsets();

  auto *preview_widget = new ControlPagePreviewWidget(&dlg);
  preview_widget->setStyleSheet(
      "QWidget { background:#07111d; border:1px solid #2d4b66; border-radius:8px; }");
  preview_widget->setFixedSize(panel_preview_w, panel_preview_h);
    preview_widget->build_element_appearance_overrides =
      [&]() { return preview_element_appearance_overrides; };

  auto *sidebar_host = new QWidget(&dlg);
  sidebar_host->setFixedWidth(sidebar_w);
  auto *sidebar = new QVBoxLayout(sidebar_host);
  sidebar->setContentsMargins(0, 0, 0, 0);
  sidebar->setSpacing(12);

  auto *editor_group = new QGroupBox(QStringLiteral("Element Editor"), sidebar_host);
  auto *editor_layout = new QFormLayout(editor_group);
  editor_layout->setContentsMargins(10, 10, 10, 10);
  editor_layout->setSpacing(8);

  auto *editor_hint = new QLabel(
      QStringLiteral("Click any visible control in the preview to adjust its frame."),
      editor_group);
  editor_hint->setWordWrap(true);
  auto *preview_mode_combo = new QComboBox(editor_group);
  preview_mode_combo->addItem(QStringLiteral("Simple"), 0);
  preview_mode_combo->addItem(QStringLiteral("Detail"), 1);
  preview_mode_combo->setCurrentIndex(preview_ctrl.show_detailed_ctrl ? 1 : 0);
  auto *selected_element_label = new QLabel(QStringLiteral("None"), editor_group);
  auto *selected_rect_label = new QLabel(QStringLiteral("Rect: -"), editor_group);
  auto *offset_w = new QSpinBox(editor_group);
  auto *offset_h = new QSpinBox(editor_group);
  for (QSpinBox *spin : {offset_w, offset_h}) {
    spin->setRange(-400, 400);
    spin->setSingleStep(1);
    spin->setEnabled(false);
  }
  auto *reset_selected_btn = new QPushButton(QStringLiteral("Reset Selected"), editor_group);
  auto *reset_all_btn = new QPushButton(QStringLiteral("Reset All Layout"), editor_group);
  auto *part_combo = new QComboBox(editor_group);
  part_combo->addItem(QStringLiteral("Text"), 0);
  part_combo->addItem(QStringLiteral("Slider"), 1);
  part_combo->addItem(QStringLiteral("Input"), 2);
  auto *part_w = new QSpinBox(editor_group);
  auto *part_h = new QSpinBox(editor_group);
  for (QSpinBox *spin : {part_w, part_h}) {
    spin->setRange(-400, 400);
    spin->setSingleStep(1);
    spin->setEnabled(false);
  }
  reset_selected_btn->setEnabled(false);
  part_combo->setEnabled(false);

  editor_layout->addRow(editor_hint);
  editor_layout->addRow(QStringLiteral("Page"), preview_mode_combo);
  editor_layout->addRow(QStringLiteral("Selected"), selected_element_label);
  editor_layout->addRow(QStringLiteral("Current Rect"), selected_rect_label);
  editor_layout->addRow(QStringLiteral("Delta W"), offset_w);
  editor_layout->addRow(QStringLiteral("Delta H"), offset_h);
  editor_layout->addRow(QStringLiteral("Part"), part_combo);
  editor_layout->addRow(QStringLiteral("Part Delta W"), part_w);
  editor_layout->addRow(QStringLiteral("Part Delta H"), part_h);
  editor_layout->addRow(reset_selected_btn, reset_all_btn);

  auto *style_group = new QGroupBox(QStringLiteral("Appearance"), sidebar_host);
  auto *layout = new QFormLayout(style_group);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(10);

  auto *live_preview_cb = new QCheckBox(tr("style.live_preview"), &dlg);
  live_preview_cb->setChecked(true);

  auto make_scale_row = [&](int init_percent) {
    auto *slider = new QSlider(Qt::Horizontal, &dlg);
    slider->setRange(70, 500);
    slider->setSingleStep(1);
    slider->setValue(init_percent);
    auto *value_input = new QSpinBox(&dlg);
    value_input->setRange(70, 500);
    value_input->setSuffix("%");
    value_input->setValue(init_percent);
    value_input->setSingleStep(1);
    value_input->setAlignment(Qt::AlignRight);
    value_input->setButtonSymbols(QAbstractSpinBox::PlusMinus);

    connect(slider, &QSlider::valueChanged, &dlg, [value_input](int v) {
      if (value_input->value() != v)
        value_input->setValue(v);
    });
    connect(value_input, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
            [slider](int v) {
              if (slider->value() != v)
                slider->setValue(v);
    });

    auto *row = new QWidget(&dlg);
    auto *row_layout = new QVBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(4);

    auto *main_row = new QWidget(row);
    auto *main_layout = new QHBoxLayout(main_row);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(slider, 1);
    main_layout->addWidget(value_input, 0);
    row_layout->addWidget(main_row);

    auto *quick_row = new QWidget(row);
    auto *quick_layout = new QHBoxLayout(quick_row);
    quick_layout->setContentsMargins(0, 0, 0, 0);
    quick_layout->setSpacing(4);
    const int quick_values[] = {75, 100, 125, 150, 200};
    for (int v : quick_values) {
      auto *btn = new QPushButton(QString("%1%").arg(v), quick_row);
      btn->setAutoDefault(false);
      btn->setDefault(false);
      btn->setFixedHeight(22);
      btn->setMinimumWidth(52);
      connect(btn, &QPushButton::clicked, &dlg, [slider, v]() {
        slider->setValue(v);
      });
      quick_layout->addWidget(btn);
    }
    quick_layout->addStretch(1);
    row_layout->addWidget(quick_row);

    return std::make_pair(row, slider);
  };

  auto master_pair =
      make_scale_row((int)std::lround(control_text_scale_ * 100.0));
  auto caption_pair =
      make_scale_row((int)std::lround(control_caption_scale_ * 100.0));
  auto switch_pair =
      make_scale_row((int)std::lround(control_switch_option_scale_ * 100.0));
  auto value_pair =
      make_scale_row((int)std::lround(control_value_scale_ * 100.0));

  auto *accent_btn =
      new QPushButton(tr("style.accent"), &dlg);
  auto *border_btn =
      new QPushButton(tr("style.border"), &dlg);
  auto *text_btn =
      new QPushButton(tr("style.text_primary"), &dlg);
    auto *dim_btn = new QPushButton(tr("style.text_dim"), &dlg);
    auto *custom_btn = new QPushButton(tr("style.custom_color"), &dlg);

  QColor pending_accent = control_accent_color_;
  QColor pending_border = control_border_color_;
  QColor pending_text = control_text_color_;
  QColor pending_dim = control_dim_text_color_;

  auto current_text_scale = [&]() {
    return std::max(0.70, std::min(1.50, (double)master_pair.second->value() / 100.0));
  };
  auto current_caption_scale = [&]() {
    return std::max(0.70, std::min(1.50, (double)caption_pair.second->value() / 100.0));
  };
  auto current_switch_scale = [&]() {
    return std::max(0.70, std::min(1.50, (double)switch_pair.second->value() / 100.0));
  };
  auto current_value_scale = [&]() {
    return std::max(0.70, std::min(1.50, (double)value_pair.second->value() / 100.0));
  };

  auto build_preview_input = [&]() {
    MapControlPanelInput in;
    in.ctrl = preview_ctrl;
    in.ctrl.running_ui = false;
    in.ctrl.crossbow_unlocked = crossbow_unlocked;
    in.time_info = time_info_;
    in.language = ui_language_;
    const bool selected_scope =
        (preview_widget->selected_element != CTRL_LAYOUT_ELEMENT_NONE);
    if (selected_scope) {
      in.control_text_scale = control_text_scale_;
      in.caption_text_scale = control_caption_scale_;
      in.switch_option_text_scale = control_switch_option_scale_;
      in.value_text_scale = control_value_scale_;
      in.accent_color = control_accent_color_;
      in.border_color = control_border_color_;
      in.text_color = control_text_color_;
      in.dim_text_color = control_dim_text_color_;
    } else {
      in.control_text_scale = current_text_scale();
      in.caption_text_scale = current_caption_scale();
      in.switch_option_text_scale = current_switch_scale();
      in.value_text_scale = current_value_scale();
      in.accent_color = pending_accent;
      in.border_color = pending_border;
      in.text_color = pending_text;
      in.dim_text_color = pending_dim;
    }
    in.layout_overrides = &preview_layout_overrides;
    in.slider_part_overrides = &preview_slider_part_overrides;
    in.rnx_name_bds = QString::fromUtf8(in.ctrl.rinex_name_bds[0] ? in.ctrl.rinex_name_bds : "N/A");
    in.rnx_name_gps = QString::fromUtf8(in.ctrl.rinex_name_gps[0] ? in.ctrl.rinex_name_gps : "N/A");
    return in;
  };

  preview_widget->build_input = build_preview_input;

  auto render_preview = [&]() {
    preview_widget->update();
  };

  auto apply_btn_color = [](QPushButton *btn, const QColor &c) {
    if (!btn || !c.isValid())
      return;
    btn->setStyleSheet(
        QString("QPushButton{background:%1;color:#06111f;border:1px solid "
                "#b9cadf;padding:4px 8px;}")
            .arg(c.name()));
  };
  apply_btn_color(accent_btn, pending_accent);
  apply_btn_color(border_btn, pending_border);
  apply_btn_color(text_btn, pending_text);
  apply_btn_color(dim_btn, pending_dim);

  auto clear_all_element_appearance_overrides = [&]() {
    for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
      preview_element_appearance_overrides[i] = ControlElementAppearanceOverride{};
    }
  };

  auto update_selected_element_appearance_override = [&]() {
    const ControlLayoutElementId id = preview_widget->selected_element;
    if (id == CTRL_LAYOUT_ELEMENT_NONE) {
      return;
    }
    ControlElementAppearanceOverride &ov =
        preview_element_appearance_overrides[id];
    ov.enabled = true;
    ov.master_scale = current_text_scale();
    ov.caption_scale = current_caption_scale();
    ov.switch_scale = current_switch_scale();
    ov.value_scale = current_value_scale();
    ov.accent_color = pending_accent;
    ov.border_color = pending_border;
    ov.text_color = pending_text;
    ov.dim_text_color = pending_dim;
  };

  auto apply_preview = [&]() {
    const bool selected_scope =
        (preview_widget->selected_element != CTRL_LAYOUT_ELEMENT_NONE);
    if (selected_scope) {
      update_selected_element_appearance_override();
    } else {
      clear_all_element_appearance_overrides();
      control_text_scale_ = current_text_scale();
      control_caption_scale_ = current_caption_scale();
      control_switch_option_scale_ = current_switch_scale();
      control_value_scale_ = current_value_scale();
      if (pending_accent.isValid()) control_accent_color_ = pending_accent;
      if (pending_border.isValid()) control_border_color_ = pending_border;
      if (pending_text.isValid()) control_text_color_ = pending_text;
      if (pending_dim.isValid()) control_dim_text_color_ = pending_dim;
    }
    sanitize_position_offsets();
    control_layout_overrides_ = preview_layout_overrides;
    control_slider_part_overrides_ = preview_slider_part_overrides;
    for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
      control_element_appearance_overrides_[i] =
          preview_element_appearance_overrides[i];
    }
    update(osm_panel_rect_);
    update(right_bottom_region());
  };

  auto is_slider_element = [](ControlLayoutElementId id) {
    switch (id) {
    case CTRL_LAYOUT_ELEMENT_TX_SLIDER:
    case CTRL_LAYOUT_ELEMENT_GAIN_SLIDER:
    case CTRL_LAYOUT_ELEMENT_FS_SLIDER:
    case CTRL_LAYOUT_ELEMENT_CN0_SLIDER:
    case CTRL_LAYOUT_ELEMENT_HEIGHT_SLIDER:
    case CTRL_LAYOUT_ELEMENT_PRN_SLIDER:
    case CTRL_LAYOUT_ELEMENT_PATH_V_SLIDER:
    case CTRL_LAYOUT_ELEMENT_PATH_A_SLIDER:
    case CTRL_LAYOUT_ELEMENT_CH_SLIDER:
      return true;
    default:
      return false;
    }
  };

  auto selected_part_adjustment = [&](ControlLayoutElementId id)
      -> ControlRectAdjustment * {
    if (id == CTRL_LAYOUT_ELEMENT_NONE || !is_slider_element(id)) {
      return nullptr;
    }
    const int part = part_combo->currentData().toInt();
    if (part == 0) return &preview_slider_part_overrides.label[id];
    if (part == 1) return &preview_slider_part_overrides.track[id];
    return &preview_slider_part_overrides.value[id];
  };

  auto update_selected_rect_summary = [&](ControlLayoutElementId id) {
    if (id == CTRL_LAYOUT_ELEMENT_NONE) {
      selected_rect_label->setText(QStringLiteral("Rect: -"));
      return;
    }
    const MapControlPanelInput in = build_preview_input();
    ControlLayout lo;
    compute_control_layout(preview_widget->layout_width(),
                           preview_widget->layout_height(), &lo,
                           in.ctrl.show_detailed_ctrl,
                           in.ctrl.signal_mode != SIG_MODE_MIXED,
                           in.layout_overrides);
    const Rect *rect = control_layout_rect(&lo, id);
    if (!rect || rect->w <= 0 || rect->h <= 0) {
      selected_rect_label->setText(QStringLiteral("Rect: hidden"));
      return;
    }
    selected_rect_label->setText(
        QString("Rect: %1, %2, %3 x %4")
            .arg(rect->x)
            .arg(rect->y)
            .arg(rect->w)
            .arg(rect->h));
  };

  auto sync_preview_widget = [&]() {
    render_preview();
    update_selected_rect_summary(preview_widget->selected_element);
  };

  auto trigger_live_preview = [&]() {
    if (preview_widget->selected_element != CTRL_LAYOUT_ELEMENT_NONE) {
      update_selected_element_appearance_override();
    }
    sync_preview_widget();
    if (live_preview_cb->isChecked()) {
      apply_preview();
    }
  };

  auto sync_appearance_controls_from_scope = [&]() {
    const ControlLayoutElementId id = preview_widget->selected_element;
    bool has_selected_style = false;
    if (id != CTRL_LAYOUT_ELEMENT_NONE) {
      const ControlElementAppearanceOverride &ov =
          preview_element_appearance_overrides[id];
      has_selected_style = ov.enabled;
      if (has_selected_style) {
        QSignalBlocker b_master(master_pair.second);
        QSignalBlocker b_caption(caption_pair.second);
        QSignalBlocker b_switch(switch_pair.second);
        QSignalBlocker b_value(value_pair.second);
        if (ov.master_scale > 0.0)
          master_pair.second->setValue((int)std::lround(ov.master_scale * 100.0));
        if (ov.caption_scale > 0.0)
          caption_pair.second->setValue((int)std::lround(ov.caption_scale * 100.0));
        if (ov.switch_scale > 0.0)
          switch_pair.second->setValue((int)std::lround(ov.switch_scale * 100.0));
        if (ov.value_scale > 0.0)
          value_pair.second->setValue((int)std::lround(ov.value_scale * 100.0));
        if (ov.accent_color.isValid())
          pending_accent = ov.accent_color;
        if (ov.border_color.isValid())
          pending_border = ov.border_color;
        if (ov.text_color.isValid())
          pending_text = ov.text_color;
        if (ov.dim_text_color.isValid())
          pending_dim = ov.dim_text_color;
      }
    }
    if (!has_selected_style) {
      QSignalBlocker b_master(master_pair.second);
      QSignalBlocker b_caption(caption_pair.second);
      QSignalBlocker b_switch(switch_pair.second);
      QSignalBlocker b_value(value_pair.second);
      master_pair.second->setValue((int)std::lround(control_text_scale_ * 100.0));
      caption_pair.second->setValue((int)std::lround(control_caption_scale_ * 100.0));
      switch_pair.second->setValue(
          (int)std::lround(control_switch_option_scale_ * 100.0));
      value_pair.second->setValue((int)std::lround(control_value_scale_ * 100.0));
      pending_accent = control_accent_color_;
      pending_border = control_border_color_;
      pending_text = control_text_color_;
      pending_dim = control_dim_text_color_;
    }
    apply_btn_color(accent_btn, pending_accent);
    apply_btn_color(border_btn, pending_border);
    apply_btn_color(text_btn, pending_text);
    apply_btn_color(dim_btn, pending_dim);
  };

  connect(preview_mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          &dlg, [&](int idx) {
            preview_ctrl.show_detailed_ctrl = (idx == 1);
            preview_widget->selected_element = CTRL_LAYOUT_ELEMENT_NONE;
            trigger_live_preview();
          });

  auto set_selected_element = [&](ControlLayoutElementId id) {
    preview_widget->selected_element = id;
    const bool has_selection = (id != CTRL_LAYOUT_ELEMENT_NONE);
    const bool part_edit_enabled = has_selection && is_slider_element(id);
    reset_selected_btn->setEnabled(has_selection);
    selected_element_label->setText(
        has_selection ? QString::fromUtf8(control_layout_element_debug_name(id))
                      : QStringLiteral("None"));
    for (QSpinBox *spin : {offset_w, offset_h}) {
      spin->setEnabled(has_selection);
    }
    part_combo->setEnabled(part_edit_enabled);
    for (QSpinBox *spin : {part_w, part_h}) {
      spin->setEnabled(part_edit_enabled);
    }
    if (!has_selection) {
      QSignalBlocker b3(offset_w);
      QSignalBlocker b4(offset_h);
      offset_w->setValue(0);
      offset_h->setValue(0);
      part_w->setValue(0);
      part_h->setValue(0);
      update_selected_rect_summary(id);
      render_preview();
      return;
    }

    const ControlRectAdjustment &adj = preview_layout_overrides.entries[id];
    QSignalBlocker b3(offset_w);
    QSignalBlocker b4(offset_h);
    offset_w->setValue(adj.dw);
    offset_h->setValue(adj.dh);

    if (part_edit_enabled) {
      const ControlRectAdjustment *part_adj = selected_part_adjustment(id);
      if (part_adj) {
        part_w->setValue(part_adj->dw);
        part_h->setValue(part_adj->dh);
      }
    } else {
      part_w->setValue(0);
      part_h->setValue(0);
    }

    update_selected_rect_summary(id);
    sync_appearance_controls_from_scope();
    render_preview();
  };

  preview_widget->on_select = [&](ControlLayoutElementId id) {
    set_selected_element(id);
  };

  auto update_selected_adjustment = [&]() {
    const ControlLayoutElementId id = preview_widget->selected_element;
    if (id == CTRL_LAYOUT_ELEMENT_NONE) {
      return;
    }
    ControlRectAdjustment &adj = preview_layout_overrides.entries[id];
    adj.dx = 0;
    adj.dy = 0;
    adj.dw = offset_w->value();
    adj.dh = offset_h->value();
    trigger_live_preview();
  };

  auto update_selected_part_adjustment = [&]() {
    const ControlLayoutElementId id = preview_widget->selected_element;
    ControlRectAdjustment *adj = selected_part_adjustment(id);
    if (!adj) {
      return;
    }
    adj->dx = 0;
    adj->dy = 0;
    adj->dw = part_w->value();
    adj->dh = part_h->value();
    trigger_live_preview();
  };

  auto pick_color = [&](QColor &target, QPushButton *btn, const QString &title) {
    QColor picked = QColorDialog::getColor(target, &dlg, title);
    if (picked.isValid()) {
      target = picked;
      apply_btn_color(btn, picked);
    }
  };

  connect(master_pair.second, &QSlider::valueChanged, &dlg,
          [&](int) { trigger_live_preview(); });
  connect(caption_pair.second, &QSlider::valueChanged, &dlg,
          [&](int) { trigger_live_preview(); });
  connect(switch_pair.second, &QSlider::valueChanged, &dlg,
          [&](int) { trigger_live_preview(); });
  connect(value_pair.second, &QSlider::valueChanged, &dlg,
          [&](int) { trigger_live_preview(); });
  connect(live_preview_cb, &QCheckBox::toggled, &dlg, [&](bool on) {
    if (on)
      apply_preview();
  });
  connect(offset_w, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
          [&](int) { update_selected_adjustment(); });
  connect(offset_h, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
          [&](int) { update_selected_adjustment(); });
    connect(part_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg,
      [&](int) { set_selected_element(preview_widget->selected_element); });
    connect(part_w, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
      [&](int) { update_selected_part_adjustment(); });
    connect(part_h, QOverload<int>::of(&QSpinBox::valueChanged), &dlg,
      [&](int) { update_selected_part_adjustment(); });
  connect(reset_selected_btn, &QPushButton::clicked, &dlg, [&]() {
    const ControlLayoutElementId id = preview_widget->selected_element;
    if (id == CTRL_LAYOUT_ELEMENT_NONE) {
      return;
    }
    preview_layout_overrides.entries[id] = ControlRectAdjustment{};
    preview_element_appearance_overrides[id] = ControlElementAppearanceOverride{};
    set_selected_element(id);
    trigger_live_preview();
  });
  connect(reset_all_btn, &QPushButton::clicked, &dlg, [&]() {
    preview_layout_overrides = ControlLayoutOverrides{};
    preview_slider_part_overrides = ControlSliderPartOverrides{};
    sanitize_position_offsets();
    clear_all_element_appearance_overrides();
    set_selected_element(preview_widget->selected_element);
    trigger_live_preview();
  });

  connect(accent_btn, &QPushButton::clicked, &dlg, [&]() {
    pick_color(pending_accent, accent_btn, tr("style.pick_accent"));
    trigger_live_preview();
  });
  connect(border_btn, &QPushButton::clicked, &dlg, [&]() {
    pick_color(pending_border, border_btn, tr("style.pick_border"));
    trigger_live_preview();
  });
  connect(text_btn, &QPushButton::clicked, &dlg, [&]() {
    pick_color(pending_text, text_btn, tr("style.pick_text_primary"));
    trigger_live_preview();
  });
  connect(dim_btn, &QPushButton::clicked, &dlg, [&]() {
    pick_color(pending_dim, dim_btn, tr("style.pick_text_dim"));
    trigger_live_preview();
  });

  connect(custom_btn, &QPushButton::clicked, &dlg, [&]() {
    QColor picked = QColorDialog::getColor(
        pending_accent, &dlg, tr("style.pick_custom"));
    if (!picked.isValid()) return;
    pending_accent = picked;
    apply_btn_color(accent_btn, pending_accent);
    trigger_live_preview();
  });

  auto *color_row = new QWidget(&dlg);
  auto *color_layout = new QHBoxLayout(color_row);
  color_layout->setContentsMargins(0, 0, 0, 0);
  color_layout->addWidget(accent_btn);
  color_layout->addWidget(border_btn);
  color_layout->addWidget(text_btn);
  color_layout->addWidget(dim_btn);
  color_layout->addWidget(custom_btn);

  // -- Optional Times New Roman font variants --
  auto *font_bold_cb       = new QCheckBox(tr("style.font.times_bold"), &dlg);
  auto *font_italic_cb     = new QCheckBox(tr("style.font.times_italic"), &dlg);
  auto *font_bold_italic_cb = new QCheckBox(tr("style.font.times_bold_italic"), &dlg);
  QCheckBox *font_zh_kai_cb = nullptr;
  font_bold_cb->setChecked(font_times_bold_);
  font_italic_cb->setChecked(font_times_italic_);
  font_bold_italic_cb->setChecked(font_times_bold_italic_);

  auto *font_row = new QWidget(&dlg);
  auto *font_vlay = new QVBoxLayout(font_row);
  font_vlay->setContentsMargins(0, 0, 0, 0);
  font_vlay->setSpacing(4);
  font_vlay->addWidget(font_bold_cb);
  font_vlay->addWidget(font_italic_cb);
  font_vlay->addWidget(font_bold_italic_cb);
  if (gui_language_is_zh_tw(ui_language_)) {
    font_zh_kai_cb = new QCheckBox(tr("style.font.zh_kai"), &dlg);
    font_zh_kai_cb->setChecked(font_zh_kai_);
    font_vlay->addWidget(font_zh_kai_cb);

    connect(font_zh_kai_cb, &QCheckBox::toggled, &dlg, [&](bool on) {
      if (!live_preview_cb->isChecked()) {
        return;
      }
      font_zh_kai_ = on;
      gui_font_set_zh_kai_enabled(font_zh_kai_);
      gui_runtime_apply_language_font(this, ui_language_);
      update();
    });
  }

  auto *reset_btn = new QPushButton(tr("style.reset_defaults"), &dlg);
  connect(reset_btn, &QPushButton::clicked, &dlg, [&]() {
    master_pair.second->setValue(100);
    caption_pair.second->setValue(75);
    switch_pair.second->setValue(150);
    value_pair.second->setValue(100);
    pending_accent = QColor("#00e5ff");
    pending_border = QColor("#b9cadf");
    pending_text = QColor("#f8fbff");
    pending_dim = QColor("#6b7b90");
    apply_btn_color(accent_btn, pending_accent);
    apply_btn_color(border_btn, pending_border);
    apply_btn_color(text_btn, pending_text);
    apply_btn_color(dim_btn, pending_dim);
    preview_layout_overrides = ControlLayoutOverrides{};
    preview_slider_part_overrides = ControlSliderPartOverrides{};
    sanitize_position_offsets();
    clear_all_element_appearance_overrides();
    set_selected_element(preview_widget->selected_element);
    trigger_live_preview();
  });

    layout->addRow(tr("style.row.master_text"),
                 master_pair.first);
    layout->addRow(tr("style.row.caption_text"),
                 caption_pair.first);
    layout->addRow(tr("style.row.switch_option_text"), switch_pair.first);
    layout->addRow(tr("style.row.value_text"),
                 value_pair.first);
    layout->addRow(tr("style.row.colors"),
                 color_row);
  layout->addRow(tr("style.row.optional_fonts"), font_row);
  layout->addRow(QString(), live_preview_cb);
  layout->addRow(QString(), reset_btn);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addRow(buttons);

  auto *sidebar_tabs = new QTabWidget(sidebar_host);
  sidebar_tabs->setDocumentMode(true);
  sidebar_tabs->addTab(editor_group, QStringLiteral("Layout"));
  sidebar_tabs->addTab(style_group, QStringLiteral("Appearance"));
  sidebar->addWidget(sidebar_tabs, 1);

  auto *body = new QWidget(&dlg);
  auto *body_layout = new QHBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(12);
  body_layout->addWidget(preview_widget, 3);
  body_layout->addWidget(sidebar_host, 0);

  root->addWidget(body, 1);

  QTimer::singleShot(0, &dlg, [&]() {
    set_selected_element(CTRL_LAYOUT_ELEMENT_NONE);
    render_preview();
  });

  if (dlg.exec() == QDialog::Accepted) {
    apply_preview();
    font_times_bold_        = font_bold_cb->isChecked();
    font_times_italic_      = font_italic_cb->isChecked();
    font_times_bold_italic_ = font_bold_italic_cb->isChecked();
    if (font_zh_kai_cb) {
      font_zh_kai_ = font_zh_kai_cb->isChecked();
    }
    gui_font_set_zh_kai_enabled(font_zh_kai_);
    gui_runtime_apply_optional_times_fonts(
        font_times_bold_, font_times_italic_, font_times_bold_italic_);
    gui_runtime_apply_language_font(this, ui_language_);
    update();
  } else if (live_preview_cb->isChecked()) {
    control_text_scale_ = original_control_text_scale;
    control_caption_scale_ = original_caption_scale;
    control_switch_option_scale_ = original_switch_option_scale;
    control_value_scale_ = original_value_scale;
    control_accent_color_ = original_accent;
    control_border_color_ = original_border;
    control_text_color_ = original_text;
    control_dim_text_color_ = original_dim;
    control_layout_overrides_ = original_layout_overrides;
    control_slider_part_overrides_ = original_slider_part_overrides;
    for (int i = 0; i < CTRL_LAYOUT_ELEMENT_COUNT; ++i) {
      control_element_appearance_overrides_[i] =
          original_element_appearance_overrides[i];
    }
    font_zh_kai_ = original_font_zh_kai;
    gui_font_set_zh_kai_enabled(font_zh_kai_);
    gui_runtime_apply_language_font(this, ui_language_);
    update(osm_panel_rect_);
    update(right_bottom_region());
  }
}

void MapWidget::update_waterfall_image() {
  map_update_waterfall_image(width(), height(), spec_snap_, &wf_.image,
                             &wf_.width, &wf_.height);
}

void MapWidget::draw_spectrum_panel(QPainter &p, int win_width, int win_height) {
  MapMonitorPanelsInput in;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    in.ctrl = g_ctrl;
  }
  in.spec_snap = spec_snap_;
  in.language = ui_language_;
  map_draw_spectrum_panel(p, win_width, win_height, in);
}

void MapWidget::draw_waterfall_panel(QPainter &p, int win_width, int win_height) {
  MapMonitorPanelsInput in;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    in.ctrl = g_ctrl;
  }
  in.waterfall_image = wf_.image;
  in.spec_snap = spec_snap_;
  in.language = ui_language_;
  map_draw_waterfall_panel(p, win_width, win_height, in);
}

void MapWidget::draw_time_panel(QPainter &p, int win_width, int win_height) {
  MapMonitorPanelsInput in;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    in.ctrl = g_ctrl;
  }
  in.spec_snap = spec_snap_;
  in.language = ui_language_;
  map_draw_time_panel(p, win_width, win_height, in);
}

void MapWidget::draw_constellation_panel(QPainter &p, int win_width,
                                         int win_height) {
  MapMonitorPanelsInput in;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    in.ctrl = g_ctrl;
  }
  in.show_drone_center =
      (in.ctrl.interference_selection == 2) && in.ctrl.running_ui;
  in.spec_snap = spec_snap_;
  in.language = ui_language_;
  map_draw_constellation_panel(p, win_width, win_height, in);
}
#endif // MAP_WIDGET_CONTROL_METHODS_INL_CONTEXT
