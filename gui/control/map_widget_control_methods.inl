void MapWidget::refresh_tick_timer() {
  if (!timer_)
    return;

  int interval_ms = 100;
  if (!isVisible() || isMinimized()) {
    interval_ms = 500;
  } else {
    bool running_ui = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
    }

    if (tutorial_overlay_visible_) {
      // Keep tutorial animation smooth and avoid stale partial redraws.
      interval_ms = 16;
    } else if (running_ui) {
      interval_ms = 60;
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
  QString rnx_raw = in.ctrl.rinex_name[0] ? QString::fromUtf8(in.ctrl.rinex_name)
                                          : QString("N/A");
  int slash_pos =
      std::max(rnx_raw.lastIndexOf('/'), rnx_raw.lastIndexOf('\\'));
  in.rnx_name = (slash_pos >= 0) ? rnx_raw.mid(slash_pos + 1) : rnx_raw;

  ControlLayout lo;
  compute_control_layout(win_width, win_height, &lo, in.ctrl.show_detailed_ctrl);
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
  layout->addRow(QString(), live_preview_cb);
  layout->addRow(QString(), reset_btn);

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addRow(buttons);

  if (dlg.exec() == QDialog::Accepted) {
    apply_preview();
  } else if (live_preview_cb->isChecked()) {
    control_text_scale_ = original_control_text_scale;
    control_caption_scale_ = original_caption_scale;
    control_switch_option_scale_ = original_switch_option_scale;
    control_value_scale_ = original_value_scale;
    control_accent_color_ = original_accent;
    control_border_color_ = original_border;
    control_text_color_ = original_text;
    control_dim_text_color_ = original_dim;
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
