void MapWidget::init_search_and_geo_modules() {
  // =========================================================
  // Search box, result list, and related network modules.
  // =========================================================
  search_box_ = new QLineEdit(this);
  search_box_->setAttribute(Qt::WA_InputMethodEnabled, true);
  gui_runtime_apply_search_placeholder(search_box_, ui_language_);
  search_box_->setGeometry(10, 10, 240, 30);
  search_box_->setStyleSheet(
      "background-color: rgba(18, 28, 45, 240); color: #c4d2e4; border: 1px "
      "solid rgb(80, 120, 160); border-radius: 6px; padding: 2px 6px; font-size: "
      "14px;");

  search_results_list_ = new QListWidget(this);
  search_results_list_->setVisible(false);
  search_results_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  search_results_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  search_results_list_->setWordWrap(true);
  search_results_list_->setGeometry(10, 46, 360, 220);
  search_results_list_->setStyleSheet(
      "QListWidget {"
      " background-color: rgba(18, 28, 45, 250);"
      " color: #c4d2e4;"
      " border: 1px solid rgb(80, 120, 160);"
      " border-radius: 6px;"
      " font-size: 13px;"
      " padding: 4px;"
      "}"
      "QListWidget::item {"
      " padding: 5px 7px;"
      " border-radius: 4px;"
      "}"
      "QListWidget::item:selected {"
      " background: rgb(80, 120, 160);"
      " color: #c4d2e4;"
      "}");

  alert_overlay_ = new QLabel(this);
  alert_overlay_->hide();
  alert_overlay_->setWordWrap(true);
  alert_overlay_->setAlignment(Qt::AlignCenter);
  alert_overlay_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  alert_overlay_->setFocusPolicy(Qt::NoFocus);

  connect(search_results_list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem *item) {
            if (!item)
              return;
            apply_search_result_selection(item);
          });

  connect(search_results_list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem *item) {
            if (!item)
              return;
            apply_search_result_selection(item);
          });

  search_net_ = new QNetworkAccessManager(this);
  geo_net_ = new QNetworkAccessManager(this);
  search_suggest_timer_ = new QTimer(this);
  search_suggest_timer_->setSingleShot(true);
  search_suggest_timer_->setInterval(280);
  connect(search_suggest_timer_, &QTimer::timeout, this, [this]() {
    if (is_jam_map_locked())
      return;
    const QString query = pending_search_query_.trimmed();
    if (query.isEmpty())
      return;
    if (search_box_ && search_box_->text().trimmed() != query)
      return;
    issue_place_search(query, true);
  });

  {
    QNetworkRequest req(QUrl("https://ipapi.co/json/"));
    req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");
    QNetworkReply *reply = geo_net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
      if (reply->error() == QNetworkReply::NoError) {
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (doc.isObject()) {
          const QJsonObject obj = doc.object();
          double lat = 0.0;
          double lon = 0.0;
          bool ok = false;

          if (obj.contains("latitude") && obj.contains("longitude")) {
            lat = obj.value("latitude").toDouble(0.0);
            lon = obj.value("longitude").toDouble(0.0);
            ok = true;
          } else if (obj.contains("lat") && obj.contains("lon")) {
            lat = obj.value("lat").toDouble(0.0);
            lon = obj.value("lon").toDouble(0.0);
            ok = true;
          }

          if (ok && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 &&
              lon <= 180.0) {
            user_geo_lat_deg_ = lat;
            user_geo_lon_deg_ = lon;
            user_geo_valid_ = true;

            user_geo_bootstrap_done_ = true;

            if (!user_map_interacted_) {
              osm_zoom_ = 12;
              osm_zoom_base_ = 12;
              osm_center_px_x_ = osm_lon_to_world_x(lon, osm_zoom_);
              osm_center_px_y_ = osm_lat_to_world_y(lat, osm_zoom_);
              normalize_osm_center();
              request_visible_tiles();
              notify_nfz_viewport_changed();
              osm_bg_needs_redraw_ = true;
              update(osm_panel_rect_);
            }
          }
        }
      }
      if (!user_geo_bootstrap_done_) {
        user_geo_bootstrap_done_ = true;
      }
      reply->deleteLater();
    });
  }

  connect(search_box_, &QLineEdit::returnPressed, this, [this]() {
    if (is_jam_map_locked()) {
      hide_search_results();
      search_box_->clearFocus();
      return;
    }

    if (search_results_list_ && search_results_list_->isVisible()) {
      QListWidgetItem *current =
          map_search_current_or_first(search_results_list_);
      if (current) {
        apply_search_result_selection(current);
        return;
      }
    }

    QString query = search_box_->text().trimmed();
    if (query.isEmpty())
      return;

    double coord_lat = 0.0;
    double coord_lon = 0.0;
    if (map_search_parse_coordinate_query(query, &coord_lat, &coord_lon)) {
      if (!show_search_return_) {
        pre_search_center_x_ = osm_center_px_x_;
        pre_search_center_y_ = osm_center_px_y_;
        pre_search_zoom_ = osm_zoom_;
      }
      osm_zoom_ = 17;
      show_search_return_ = true;
      set_selected_llh_direct(coord_lat, coord_lon);
      hide_search_results();
      notify_nfz_viewport_changed();
      search_box_->clearFocus();
      return;
    }

    hide_search_results();
    if (!show_search_return_) {
      pre_search_center_x_ = osm_center_px_x_;
      pre_search_center_y_ = osm_center_px_y_;
      pre_search_zoom_ = osm_zoom_;
    }
    issue_place_search(query, false);
  });

  connect(search_box_, &QLineEdit::textEdited, this,
          [this](const QString &text) {
            if (is_jam_map_locked()) {
              hide_search_results();
              return;
            }
            pending_search_query_ = text;
            if (text.trimmed().isEmpty()) {
              hide_search_results();
              return;
            }
            if (search_suggest_timer_)
              search_suggest_timer_->start();
          });

  connect(search_net_, &QNetworkAccessManager::finished, this,
          [this](QNetworkReply *reply) {
            const int seq = reply->property("search_seq").toInt();
            if (seq != latest_search_seq_) {
              reply->deleteLater();
              return;
            }

            const bool from_suggest =
                reply->property("search_suggest").toBool();
            const QString query = reply->property("search_query").toString();
            if (from_suggest && search_box_ &&
                search_box_->text().trimmed() != query.trimmed()) {
              reply->deleteLater();
              return;
            }

            if (reply->error() == QNetworkReply::NoError) {
              const QByteArray body = reply->readAll();
              const QJsonDocument doc = QJsonDocument::fromJson(body);
              if (doc.isArray()) {
                show_search_results(doc.array(), query, from_suggest);
              }
            }

            if (!from_suggest && search_box_) {
              search_box_->setFocus();
            }
            reply->deleteLater();
          });
}
