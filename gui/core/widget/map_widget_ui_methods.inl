#ifndef MAP_WIDGET_UI_METHODS_INL_CONTEXT
// This .inl requires MapWidget declarations from main_gui.cpp include context.
// Parsed standalone by IDE diagnostics it emits false errors; leave this branch empty.
#else
void MapWidget::begin_inline_edit(int field_id) {
  if (!inline_editor_)
    return;

  char val[64] = {0};
  if (!control_value_text_for_field(field_id, val, sizeof(val)))
    return;

  bool detailed = false;
  {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    detailed = g_ctrl.show_detailed_ctrl;
  }

  ControlLayout lo;
  compute_control_layout(width(), height(), &lo, detailed,
                         g_ctrl.signal_mode != SIG_MODE_MIXED);

  Rect sr = lo.tx_slider;
  switch (field_id) {
  case CTRL_SLIDER_TX:
    sr = lo.tx_slider;
    break;
  case CTRL_SLIDER_GAIN:
    sr = lo.gain_slider;
    break;
  case CTRL_SLIDER_FS:
    sr = lo.fs_slider;
    break;
  case CTRL_SLIDER_CN0:
    sr = lo.cn0_slider;
    break;
  case CTRL_SLIDER_SEED:
    sr = lo.seed_slider;
    break;
  case CTRL_SLIDER_PRN:
    sr = lo.prn_slider;
    break;
  case CTRL_SLIDER_PATH_V:
    sr = lo.path_v_slider;
    break;
  case CTRL_SLIDER_PATH_A:
    sr = lo.path_a_slider;
    break;
  case CTRL_SLIDER_CH:
    sr = lo.ch_slider;
    break;
  default:
    return;
  }

  Rect vr = slider_value_rect(sr);
  inline_edit_field_ = field_id;
  inline_editor_->setGeometry(vr.x, vr.y, vr.w, vr.h);
  inline_editor_->setText(QString::fromUtf8(val));
  inline_editor_->show();
  inline_editor_->raise();
  update_alert_overlay();
  inline_editor_->setFocus();
  inline_editor_->selectAll();
}

void MapWidget::commit_inline_edit(bool apply) {
  if (!inline_editor_ || inline_edit_field_ < 0)
    return;

  int field = inline_edit_field_;
  inline_edit_field_ = -1;

  bool changed = false;
  if (apply) {
    QByteArray ba = inline_editor_->text().trimmed().toUtf8();
    changed = handle_control_value_input(field, ba.constData());
  }

  inline_editor_->hide();
  if (changed)
    update(osm_panel_rect_);
  if (changed)
    update(right_bottom_region());
}

bool MapWidget::current_alert(QString *out_text, int *out_level) const {
  if (!out_text || !out_level) {
    return false;
  }

  std::string raw_text;
  int level = 0;
  std::chrono::steady_clock::time_point expire_tp;
  {
    std::lock_guard<std::mutex> lk(g_gui_alert_mtx);
    raw_text = g_gui_alert_text;
    level = g_gui_alert_level;
    expire_tp = g_gui_alert_expire_tp;
  }
  if (raw_text.empty() || std::chrono::steady_clock::now() > expire_tp) {
    return false;
  }

  QString alert_text = QString::fromStdString(raw_text);
  const QString i18n_prefix = QStringLiteral("__i18n__:");
  if (alert_text.startsWith(i18n_prefix)) {
    const QByteArray key_utf8 = alert_text.mid(i18n_prefix.size()).toUtf8();
    alert_text = tr_text(key_utf8.constData());
  }

  *out_text = alert_text;
  *out_level = level;
  return true;
}

void MapWidget::update_alert_overlay() {
  if (!alert_overlay_) {
    return;
  }

  QString text;
  int level = 0;
  if (!current_alert(&text, &level)) {
    alert_overlay_->hide();
    return;
  }

  QColor fill("#0f172a");
  QColor border("#64748b");
  QColor text_color("#e2e8f0");
  if (level == 1) {
    fill = QColor("#3f2f08");
    border = QColor("#f59e0b");
    text_color = QColor("#fef3c7");
  } else if (level == 2) {
    fill = QColor("#4c1d1d");
    border = QColor("#ef4444");
    text_color = QColor("#fee2e2");
  }

  QFont font = gui_font_ui_bold(ui_language_, std::max(10, std::min(13, width() / 120)));
  alert_overlay_->setFont(font);

  const int pad_x = 14;
  const int pad_y = 8;
  const int max_w = std::max(120, std::min(760, width() - 24));
  QFontMetrics fm(font);
  QRect bounds = fm.boundingRect(QRect(0, 0, max_w - pad_x * 2, 200),
                                 Qt::TextWordWrap, text);
  const int banner_w = std::min(max_w, bounds.width() + pad_x * 2);
  const int banner_h = bounds.height() + pad_y * 2;
  QRect banner((width() - banner_w) / 2, (height() - banner_h) / 2, banner_w,
               banner_h);

  alert_overlay_->setGeometry(banner);
  alert_overlay_->setText(text);
  alert_overlay_->setStyleSheet(
      QString("QLabel {"
              "background:%1;"
              "color:%2;"
              "border:1px solid %3;"
              "border-radius:10px;"
              "padding:%4px %5px;"
              "}")
          .arg(fill.name())
          .arg(text_color.name())
          .arg(border.name())
          .arg(pad_y)
          .arg(pad_x));
  if (!alert_overlay_->isVisible()) {
    alert_overlay_->show();
  }
  alert_overlay_->raise();
}

void MapWidget::update_overlay_widget_visibility() {
  if (!search_box_)
    return;

  bool should_show_search =
      map_overlay_should_show_search(tutorial_overlay_visible_);
  const bool jam_locked = is_jam_map_locked();

  if (search_box_->isVisible() != should_show_search) {
    search_box_->setVisible(should_show_search);
    if (!should_show_search) {
      search_box_->clearFocus();
      hide_search_results();
    }
  }

  const bool search_enabled =
      map_overlay_search_enabled(should_show_search, jam_locked);
  if (search_box_->isEnabled() != search_enabled) {
    search_box_->setEnabled(search_enabled);
    if (!search_enabled) {
      search_box_->clearFocus();
      hide_search_results();
    }
  }

  if (search_results_list_ && search_results_list_->isVisible() &&
      (!should_show_search || jam_locked)) {
    search_results_list_->setVisible(false);
  }
}

void MapWidget::layout_overlay_widgets(int win_width, int win_height) {
  if (!search_box_)
    return;

  const MapSearchOverlayLayout lo =
      map_overlay_search_layout(win_width, win_height);
  search_box_->setGeometry(lo.search_box_rect);
  if (search_results_list_) {
    search_results_list_->setGeometry(lo.results_list_rect);
  }
}

void MapWidget::hide_search_results() {
  map_search_hide_results(search_results_list_);
}

void MapWidget::show_search_results(const QJsonArray &arr, const QString &query,
                                    bool from_suggest) {
  if (!search_results_list_)
    return;

  bool has_bias = false;
  double bias_lat = 0.0;
  double bias_lon = 0.0;
  if (osm_panel_rect_.width() > 0 && osm_panel_rect_.height() > 0) {
    bias_lon = osm_world_x_to_lon(osm_center_px_x_, osm_zoom_);
    bias_lat = osm_world_y_to_lat(osm_center_px_y_, osm_zoom_);
    if (bias_lat >= -90.0 && bias_lat <= 90.0 && bias_lon >= -180.0 &&
        bias_lon <= 180.0) {
      has_bias = true;
    }
  }

  const std::vector<MapSearchResult> parsed =
      map_search_parse_nominatim_results(arr, query, 8, has_bias, bias_lat,
                                         bias_lon);
  const int item_count = map_search_populate_results(search_results_list_, parsed);

  if (item_count <= 0) {
    if (!from_suggest) {
      map_gui_push_alert(1, "__i18n__:search.no_match");
    } else {
      search_results_list_->setVisible(false);
    }
    return;
  }

  search_results_list_->setCurrentRow(0);
  search_results_list_->setVisible(true);
  search_results_list_->raise();
  update_alert_overlay();
  update(osm_panel_rect_);
}

QRect MapWidget::right_bottom_region() const {
  return QRect(width() / 2, height() / 2, width() - width() / 2,
               height() - height() / 2);
}

void MapWidget::apply_search_result_selection(QListWidgetItem *item) {
  if (!item)
    return;

  bool ok_lat = false;
  bool ok_lon = false;
  const double lat = item->data(Qt::UserRole).toDouble(&ok_lat);
  const double lon = item->data(Qt::UserRole + 1).toDouble(&ok_lon);
  if (!ok_lat || !ok_lon)
    return;

  if (!show_search_return_) {
    pre_search_center_x_ = osm_center_px_x_;
    pre_search_center_y_ = osm_center_px_y_;
    pre_search_zoom_ = osm_zoom_;
  }

  osm_zoom_ = 17;
  show_search_return_ = true;
  user_map_interacted_ = true;
  set_selected_llh_direct(lat, lon);
  hide_search_results();
  notify_nfz_viewport_changed();
  update(osm_panel_rect_);
  if (search_box_)
    search_box_->clearFocus();
}

void MapWidget::issue_place_search(const QString &query, bool from_suggest) {
  if (!search_net_)
    return;

  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty())
    return;

  bool has_bias = false;
  double bias_lat = 0.0;
  double bias_lon = 0.0;
  if (osm_panel_rect_.width() > 0 && osm_panel_rect_.height() > 0) {
    bias_lon = osm_world_x_to_lon(osm_center_px_x_, osm_zoom_);
    bias_lat = osm_world_y_to_lat(osm_center_px_y_, osm_zoom_);
    has_bias = (bias_lat >= -90.0 && bias_lat <= 90.0 && bias_lon >= -180.0 &&
                bias_lon <= 180.0);
  }

  const double bias_box_deg =
      std::max(0.12, std::min(3.5, 2.4 - (double)osm_zoom_ * 0.16));
  const int request_limit = from_suggest ? 14 : 18;
  const QString lang = QLocale::system().name().replace('_', '-');
  QUrl url = map_search_nominatim_url(trimmed, request_limit, has_bias, bias_lat,
                                       bias_lon, bias_box_deg, lang);

  QNetworkRequest req(url);
  req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");
  ++search_seq_counter_;
  latest_search_seq_ = search_seq_counter_;
  QNetworkReply *reply = search_net_->get(req);
  reply->setProperty("search_seq", latest_search_seq_);
  reply->setProperty("search_query", trimmed);
  reply->setProperty("search_suggest", from_suggest);
}
#endif // MAP_WIDGET_UI_METHODS_INL_CONTEXT
