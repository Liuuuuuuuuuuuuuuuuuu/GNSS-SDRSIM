void MapWidget::refresh_tick_timer() {
  if (!timer_)
    return;

  int interval_ms = 16;
  if (!isVisible() || isMinimized()) {
    interval_ms = 500;
  } else {
    bool running_ui = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
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
      // Keep tutorial animation smooth and avoid stale partial redraws.
      interval_ms = 16;
    } else if (startup_boost || map_busy) {
      // During startup and tile fill, favor fast refresh to reduce visible blanks.
      interval_ms = 16;
    } else if (running_ui) {
      // Once runtime is stable, run at lower UI tick to reduce continuous load.
      interval_ms = 33;
    } else {
      interval_ms = 20;
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
    in.rnx_name_bds = QString::fromUtf8(
      in.ctrl.rinex_name_bds[0] ? in.ctrl.rinex_name_bds : "N/A");
    in.rnx_name_gps = QString::fromUtf8(
      in.ctrl.rinex_name_gps[0] ? in.ctrl.rinex_name_gps : "N/A");

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
            rp.is_ble_rid = (t.source == QStringLiteral("bt-le-rid"));
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
  const bool original_font_zh_kai = font_zh_kai_;
  const auto tr = [&](const char *key) { return gui_i18n_text(ui_language_, key); };

  QDialog dlg(this);
  dlg.setWindowTitle(tr("style.dialog.title"));
  dlg.setModal(true);

  auto *layout = new QFormLayout(&dlg);
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

  auto apply_preview = [&]() {
    control_text_scale_ =
        std::max(0.70, std::min(1.50, (double)master_pair.second->value() / 100.0));
    control_caption_scale_ =
        std::max(0.70, std::min(1.50, (double)caption_pair.second->value() / 100.0));
    control_switch_option_scale_ =
        std::max(0.70, std::min(1.50, (double)switch_pair.second->value() / 100.0));
    control_value_scale_ =
        std::max(0.70, std::min(1.50, (double)value_pair.second->value() / 100.0));

    if (pending_accent.isValid())
      control_accent_color_ = pending_accent;
    if (pending_border.isValid())
      control_border_color_ = pending_border;
    if (pending_text.isValid())
      control_text_color_ = pending_text;
    if (pending_dim.isValid())
      control_dim_text_color_ = pending_dim;

    update(osm_panel_rect_);
    update(right_bottom_region());
  };

  auto trigger_live_preview = [&]() {
    if (live_preview_cb->isChecked()) {
      apply_preview();
    }
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
  in.spec_snap = spec_snap_;
  in.language = ui_language_;
  map_draw_constellation_panel(p, win_width, win_height, in);
}
