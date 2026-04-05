#include "main_gui.h"
#include "gui/layout/control_layout.h"
#include "gui/control/control_logic.h"
#include "gui/core/control_state.h"
#include "gui/nfz/dji_nfz.h"
#include "gui/geo/geo_io.h"
#include "gui/core/gui_screen_utils.h"
#include "gui/map/map_fallback_land.h"
#include "gui/map/map_overlay_utils.h"
#include "gui/map/map_hover_utils.h"
#include "gui/layout/map_control_panel_utils.h"
#include "gui/layout/map_monitor_panels_utils.h"
#include "gui/map/map_osm_interaction_utils.h"
#include "gui/map/map_osm_panel_utils.h"
#include "gui/map/map_render_utils.h"
#include "gui/map/map_sat_render_utils.h"
#include "gui/map/map_route_utils.h"
#include "gui/map/map_search_utils.h"
#include "gui/map/map_search_ui_utils.h"
#include "gui/map/map_tile_utils.h"
#include "gui/nfz/nfz_hit_test_utils.h"
#include "gui/geo/osm_projection.h"
#include "gui/path/path_builder.h"
#include "gui/layout/quad_panel_layout.h"
#include "gui/core/rf_mode_utils.h"
#include "gui/core/signal_snapshot.h"
#include "gui/tutorial/tutorial_interaction_utils.h"
#include "gui/tutorial/tutorial_overlay_utils.h"
#include "gui/tutorial/tutorial_flow_utils.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPen>
#include <QListWidget>
#include <QListWidgetItem>
#include <QShortcut>
#include <QRect>
#include <QScreen>
#include <QThread>
#include <QWindow>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" int enqueue_path_file_name(const char *path);
extern "C" int delete_last_queued_path(char *removed, size_t removed_sz);

extern "C" {
#include "bdssim.h"
#include "channel.h"
#include "coord.h"
#include "globals.h"
#include "orbits.h"
#include "path.h"
}

namespace {

#include "main_gui_state.inl"

class MapWidget final : public QWidget {
public:
  MapWidget() {
    g_active_widget = this;
    setWindowTitle("GNSS-SDRSIM");
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(1024, 768);

    auto toggle_fullscreen = [this]() {
      if (isFullScreen()) {
        showNormal();
        if (windowHandle()) {
          windowHandle()->setScreen(windowHandle()->screen());
        }
      } else {
        showFullScreen();
      }
      raise();
      activateWindow();
    };

    auto exit_fullscreen = [this]() {
      if (isFullScreen()) {
        showNormal();
        raise();
        activateWindow();
      }
    };

    auto *fullscreen_shortcut = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullscreen_shortcut->setContext(Qt::ApplicationShortcut);
    connect(fullscreen_shortcut, &QShortcut::activated, this, toggle_fullscreen);

    auto *exit_fullscreen_shortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    exit_fullscreen_shortcut->setContext(Qt::ApplicationShortcut);
    connect(exit_fullscreen_shortcut, &QShortcut::activated, this, exit_fullscreen);

    shp_ok_ = load_land_shapefile("./ne_50m_land/ne_50m_land.shp", shp_parts_);
    build_time_info(&time_info_);
    fetch_spectrum_snapshot(&spec_snap_);
    wf_.width = 0;
    wf_.height = 0;
    stat_text_ = "Satellites: 0";

    auto now = std::chrono::steady_clock::now();
    next_time_tick_ = now;
    next_scene_tick_ = now;
    tutorial_anim_start_tp_ = now;
    tutorial_anim_step_anchor_ = -1;

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::CoarseTimer);
    connect(timer_, &QTimer::timeout, this, [this]() { this->onTick(); });
    refresh_tick_timer();

    tile_net_ = new QNetworkAccessManager(this);
    connect(tile_net_, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) { this->onTileReply(reply); });

    route_net_ = new QNetworkAccessManager(this);
    connect(route_net_, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) { this->onRouteReply(reply); });

    inline_editor_ = new QLineEdit(this);
    inline_editor_->hide();
    inline_editor_->setAlignment(Qt::AlignCenter);
    inline_editor_->setMaxLength(32);
    inline_editor_->setStyleSheet("QLineEdit {"
                                  "background:#0f1b2b;"
                                  "color:#f8fbff;"
                                  "border:1px solid #b9cadf;"
                                  "border-radius:3px;"
                                  "padding:0 2px;"
                                  "}");
    connect(inline_editor_, &QLineEdit::returnPressed, this,
            [this]() { this->commit_inline_edit(true); });
    connect(inline_editor_, &QLineEdit::editingFinished, this, [this]() {
      if (inline_editor_ && inline_editor_->isVisible())
        this->commit_inline_edit(true);
    });
    // --- 新增：初始化 DJI NFZ 管理器 ---
    dji_nfz_mgr_ = new DjiNfzManager(this, [this]() {
      this->osm_bg_needs_redraw_ = true;
      this->update(this->osm_panel_rect_); // 資料更新時重繪地圖
    });

    // ======== 新增這兩行來預設開啟 NFZ ========
    dji_nfz_mgr_->set_enabled(true);

    // 新增：設定啟動後 0.5 秒自動抓取當下視角的禁航區
    QTimer::singleShot(500, this,
                       [this]() { this->notify_nfz_viewport_changed(); });
    // ======== =========================== ========
    // =========================================================
    // --- 新增：Search Box 初始化與事件綁定 ---
    // =========================================================
    search_box_ = new QLineEdit(this);
    search_box_->setAttribute(Qt::WA_InputMethodEnabled, true);
    search_box_->setPlaceholderText("Search a location and press Enter");
    search_box_->setGeometry(10, 10, 240, 30);
    search_box_->setStyleSheet(
        "background-color: rgba(255, 255, 255, 240); color: black; border: 1px "
        "solid #486581; border-radius: 4px; padding: 2px 6px; font-size: "
        "14px;");

    search_results_list_ = new QListWidget(this);
    search_results_list_->setVisible(false);
    search_results_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    search_results_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    search_results_list_->setGeometry(10, 46, 360, 220);
    search_results_list_->setStyleSheet(
        "QListWidget {"
        " background-color: rgba(255, 255, 255, 246);"
        " color: #0f172a;"
        " border: 1px solid #486581;"
        " border-radius: 6px;"
        " font-size: 13px;"
        " padding: 4px;"
        "}"
        "QListWidget::item {"
        " padding: 5px 7px;"
        " border-radius: 4px;"
        "}"
        "QListWidget::item:selected {"
        " background: #dbeafe;"
        " color: #0f172a;"
        "}");

    connect(search_results_list_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
              if (!item) return;
              apply_search_result_selection(item);
            });

    connect(search_results_list_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) {
              if (!item) return;
              apply_search_result_selection(item);
            });

    search_net_ = new QNetworkAccessManager(this);
    geo_net_ = new QNetworkAccessManager(this);

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

            if (ok && lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
              user_geo_lat_deg_ = lat;
              user_geo_lon_deg_ = lon;
              user_geo_valid_ = true;

              if (!user_geo_bootstrap_done_ && !user_map_interacted_) {
                osm_center_px_x_ = osm_lon_to_world_x(lon, osm_zoom_);
                osm_center_px_y_ = osm_lat_to_world_y(lat, osm_zoom_);
                notify_nfz_viewport_changed();
                update(osm_panel_rect_);
              }
              user_geo_bootstrap_done_ = true;
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

      QUrl url = map_search_nominatim_url(query, 8);

      QNetworkRequest req(url);
      req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");
      ++search_seq_counter_;
      latest_search_seq_ = search_seq_counter_;
      QNetworkReply *reply = search_net_->get(req);
      reply->setProperty("search_seq", latest_search_seq_);
    });

    connect(search_box_, &QLineEdit::textEdited, this,
            [this](const QString &) { hide_search_results(); });

    connect(search_net_, &QNetworkAccessManager::finished, this,
            [this](QNetworkReply *reply) {
              const int seq = reply->property("search_seq").toInt();
              if (seq != latest_search_seq_) {
                reply->deleteLater();
                return;
              }

              search_box_->setFocus();
              if (reply->error() == QNetworkReply::NoError) {
                const QByteArray body = reply->readAll();
                const QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isArray()) {
                  show_search_results(doc.array());
                }
              }
              reply->deleteLater();
            });
    // =========================================================
  } // <== 這是 MapWidget 建構子的結尾

  bool get_default_spoof_llh(double *lat_deg, double *lon_deg,
                             double *h_m) const {
    if (!lat_deg || !lon_deg || !h_m) {
      return false;
    }
    if (!dji_nfz_mgr_) {
      return false;
    }

    double ref_lat = g_receiver_valid ? g_receiver_lat_deg : 25.0330;
    double ref_lon = g_receiver_valid ? g_receiver_lon_deg : 121.5654;
    double out_lat = 0.0;
    double out_lon = 0.0;
    if (!nfz_find_nearest_zone_center(dji_nfz_mgr_->get_zones(), ref_lat,
                                      ref_lon, &out_lat, &out_lon)) {
      return false;
    }

    *lat_deg = out_lat;
    *lon_deg = out_lon;
    *h_m = 0.0;
    return true;
  }

  void set_selected_llh_direct_public(double lat_deg, double lon_deg,
                                      double h_m) {
    if (QThread::currentThread() != this->thread()) {
      QMetaObject::invokeMethod(
          this,
          [this, lat_deg, lon_deg, h_m]() {
            this->set_selected_llh_direct_public(lat_deg, lon_deg, h_m);
          },
          Qt::QueuedConnection);
      return;
    }
    set_selected_llh_direct(lat_deg, lon_deg, h_m, false);
    notify_nfz_viewport_changed();
    update(osm_panel_rect_);
  }

protected:
  void paintEvent(QPaintEvent *event) override {
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

    // 1. 左半邊全部都是 OSM 地圖
    QRect osm_rect(0, 0, win_width / 2, win_height);
    if (dirty_intersects(osm_rect)) {
      draw_osm_panel(p, osm_rect);
    }

    // 2. 右上角：永遠保留原本的星下點 (Satellite Map)
    // Always call draw_map_panel and rely on clip region to avoid missed redraws.
    QRect map_rect(win_width / 2, 0, win_width - win_width / 2, win_height / 2);
    draw_map_panel(p, map_rect);

    bool running_ui = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
    }

    bool tutorial_waveform_preview =
      tutorial_overlay_visible_ && !running_ui &&
      (tutorial_step_ >= 9 && tutorial_step_ <= 12);

    QRect bottom_right_rect(win_width / 2, win_height / 2,
                            win_width - win_width / 2,
                            win_height - win_height / 2);

    // 3. 右下角動態切換：未執行時顯示 Control Panel，執行時顯示四個波形圖
    if (!running_ui && !tutorial_waveform_preview) {
        if (dirty_intersects(bottom_right_rect)) {
        draw_control_panel(p, win_width, win_height);
        }
    } else {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, false);
        QRect spectrum_rect(panel_x, panel_y, panel_w, panel_h);
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, true);
        QRect waterfall_rect(panel_x, panel_y, panel_w, panel_h);
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, false);
        QRect time_rect(panel_x, panel_y, panel_w, panel_h);
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y,
                             &panel_w, &panel_h, true);
        QRect constellation_rect(panel_x, panel_y, panel_w, panel_h);

        if (dirty_intersects(spectrum_rect)) {
          draw_spectrum_panel(p, win_width, win_height);
        }
        if (dirty_intersects(waterfall_rect)) {
          draw_waterfall_panel(p, win_width, win_height);
        }
        if (dirty_intersects(time_rect)) {
          draw_time_panel(p, win_width, win_height);
        }
        if (dirty_intersects(constellation_rect)) {
          draw_constellation_panel(p, win_width, win_height);
        }
    }

    if (tutorial_overlay_visible_ || dirty_intersects(bottom_right_rect) ||
        dirty_intersects(osm_rect) || dirty_intersects(map_rect)) {
      draw_tutorial_overlay(p, win_width, win_height);
    }
    draw_hover_help_overlay(p, win_width, win_height);
    draw_alert_banner(p, win_width, win_height);
  }

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    layout_overlay_widgets(width(), height());
    map_panel_bootstrap_redraw_done_ = false;
    update(QRect(width() / 2, 0, width() - width() / 2, height() / 2));
  }

  void mousePressEvent(QMouseEvent *event) override {
    bool running_ui = false;
    bool detailed = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
      detailed = g_ctrl.show_detailed_ctrl;
    }

    if (event->button() == Qt::LeftButton) {
      if (tutorial_handle_click(event->pos(), running_ui, tutorial_last_step(),
                                tutorial_toggle_rect_, tutorial_prev_btn_rect_,
                                tutorial_next_btn_rect_, tutorial_close_btn_rect_,
                                &tutorial_enabled_, &tutorial_overlay_visible_,
                                &tutorial_step_, &tutorial_anim_step_anchor_,
                                &tutorial_spotlight_index_, &tutorial_text_page_,
                                &tutorial_text_page_count_,
                                &tutorial_text_page_anchor_step_,
                                &tutorial_text_page_anchor_spotlight_)) {
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
      rects.osm_stop_btn_rect = osm_stop_btn_rect_;
      rects.dark_mode_btn_rect = dark_mode_btn_rect_;
      rects.search_return_btn_rect = search_return_btn_rect_;
      rects.nfz_btn_rect = nfz_btn_rect_;
      rects.back_btn_rect = back_btn_rect_;
      rects.show_search_return = show_search_return_;

      MapOsmPressState state;
      state.dragging_osm = &dragging_osm_;
      state.drag_moved_osm = &drag_moved_osm_;
      state.drag_last_pos = &drag_last_pos_;

      MapOsmPressActions actions;
      actions.stop_simulation = [this]() {
        {
          std::lock_guard<std::mutex> lk(g_ctrl_mtx);
          g_ctrl.running_ui = false;
        }
        g_runtime_abort = 1;
        g_gui_stop_req.fetch_add(1);
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
      MapOsmPressState state;
      MapOsmPressActions actions;
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

  void mouseDoubleClickEvent(QMouseEvent *event) override {
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

  void mouseMoveEvent(QMouseEvent *event) override {
    if (!(event->buttons() & Qt::LeftButton)) {
      update_hover_help(event->pos());
    }

    if (active_control_slider_ >= 0 && (event->buttons() & Qt::LeftButton)) {
      clear_hover_help();
      if (handle_control_slider_drag(active_control_slider_, event->pos().x(),
                                     width(), height())) {
        update(osm_panel_rect_);
        update(right_bottom_region());
      }
      event->accept();
      return;
    }

    if (map_osm_handle_move(
            event->pos(), event->buttons(), osm_panel_rect_,
            is_jam_map_locked(), &dragging_osm_, &drag_moved_osm_,
            &drag_last_pos_, &osm_center_px_x_, &osm_center_px_y_,
            [this]() { clear_hover_help(); },
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
    QWidget::mouseMoveEvent(event);
  }

  void leaveEvent(QEvent *event) override {
    clear_hover_help();
    QWidget::leaveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      active_control_slider_ = -1;
      bool running_ui = false;
      {
        std::lock_guard<std::mutex> lk(g_ctrl_mtx);
        running_ui = g_ctrl.running_ui;
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
                                         clk_lon, &target_lat, &target_lon)) {
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
      dragging_osm_ = false;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    QPoint p0 = event->position().toPoint();
    int delta = event->angleDelta().y();
    if (map_osm_handle_wheel(
            p0, delta, osm_panel_rect_, is_jam_map_locked(), &osm_zoom_,
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

  void closeEvent(QCloseEvent *event) override {
    commit_inline_edit(true);
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      g_ctrl.running_ui = false;
    }
    // Always follow the same safe shutdown path regardless of close method.
    g_runtime_abort = 1;
    g_gui_stop_req.fetch_add(1);
    g_gui_exit_req.fetch_add(1);
    g_running.store(false);
    event->accept();
    QWidget::closeEvent(event);
  }

  void begin_inline_edit(int field_id) {
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
    compute_control_layout(width(), height(), &lo, detailed);

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
    inline_editor_->setFocus();
    inline_editor_->selectAll();
  }

  void commit_inline_edit(bool apply) {
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

  void clear_hover_help() {
    if (hover_help_visible_ || hover_region_id_ >= 0) {
      QRect dirty = hover_help_invalid_rect(hover_help_anchor_rect_);
      hover_help_visible_ = false;
      hover_help_text_.clear();
      hover_region_id_ = -1;
      if (!dirty.isEmpty())
        update(dirty);
    }
  }

  int hover_region_for_pos(const QPoint &pos, QString *text, QRect *anchor) const {
    MapHoverHelpInput in;
    in.win_width = width();
    in.win_height = height();
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.dark_map_mode = dark_map_mode_;
    in.show_search_return = show_search_return_;
    in.search_box_visible = search_box_ && search_box_->isVisible();
    return map_hover_region_for_pos(pos, in, text, anchor);
  }

  void update_hover_help(const QPoint &pos) {
    QString text;
    QRect anchor;
    const int region_id = hover_region_for_pos(pos, &text, &anchor);
    const bool visible = region_id >= 0;

    if (region_id == hover_region_id_ && visible == hover_help_visible_ &&
        text == hover_help_text_ && anchor == hover_help_anchor_rect_) {
      return;
    }

    hover_region_id_ = region_id;
    hover_help_visible_ = visible;
    hover_help_text_ = text;
    QRect old_anchor = hover_help_anchor_rect_;
    hover_help_anchor_rect_ = anchor;
    if (visible) {
      hover_enter_tp_ = std::chrono::steady_clock::now();
    }
    QRect dirty = hover_help_invalid_rect(anchor);
    if (!old_anchor.isEmpty())
      dirty = dirty.united(hover_help_invalid_rect(old_anchor));
    if (!dirty.isEmpty())
      update(dirty);
  }

  void draw_hover_help_overlay(QPainter &p, int win_width, int win_height) {
    MapHoverHelpInput in;
    in.win_width = win_width;
    in.win_height = win_height;
    in.tutorial_overlay_visible = tutorial_overlay_visible_;
    in.dark_map_mode = dark_map_mode_;
    in.show_search_return = show_search_return_;
    in.search_box_visible = search_box_ && search_box_->isVisible();
    in.search_box_rect = search_box_ ? search_box_->geometry() : QRect();
    in.search_return_btn_rect = search_return_btn_rect_;
    in.dark_mode_btn_rect = dark_mode_btn_rect_;
    in.nfz_btn_rect = nfz_btn_rect_;
    in.tutorial_toggle_rect = tutorial_toggle_rect_;
    in.back_btn_rect = back_btn_rect_;
    in.osm_stop_btn_rect = osm_stop_btn_rect_;
    in.osm_runtime_rect = osm_runtime_rect_;
    in.osm_panel_rect = osm_panel_rect_;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.nfz_on = dji_nfz_mgr_ && dji_nfz_mgr_->is_enabled();
    map_draw_hover_help_overlay(p, in, hover_help_visible_, hover_help_text_,
                                hover_help_anchor_rect_);
  }

  void draw_alert_banner(QPainter &p, int win_width, int /*win_height*/) {
    std::string text;
    int level = 0;
    std::chrono::steady_clock::time_point expire_tp;
    {
      std::lock_guard<std::mutex> lk(g_gui_alert_mtx);
      text = g_gui_alert_text;
      level = g_gui_alert_level;
      expire_tp = g_gui_alert_expire_tp;
    }
    if (text.empty() || std::chrono::steady_clock::now() > expire_tp) {
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

    QFont old_font = p.font();
    QFont font = old_font;
    font.setFamily("Noto Sans");
    font.setBold(true);
    font.setPointSize(std::max(10, std::min(13, win_width / 120)));
    p.setFont(font);

    const QString banner_text = QString::fromStdString(text);
    const int pad_x = 14;
    const int pad_y = 8;
    const int max_w = std::min(760, win_width - 24);
    QRect bounds = p.fontMetrics().boundingRect(
        QRect(0, 0, max_w - pad_x * 2, 200), Qt::TextWordWrap, banner_text);
    QRect banner((win_width - std::min(max_w, bounds.width() + pad_x * 2)) / 2,
                 12,
                 std::min(max_w, bounds.width() + pad_x * 2),
                 bounds.height() + pad_y * 2);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(banner, 10, 10);
    p.setPen(text_color);
    p.drawText(banner.adjusted(pad_x, pad_y, -pad_x, -pad_y),
               Qt::AlignCenter | Qt::TextWordWrap, banner_text);
    p.setFont(old_font);
    p.setRenderHint(QPainter::Antialiasing, false);
  }

private:
  void update_overlay_widget_visibility() {
    if (!search_box_) return;

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

  void layout_overlay_widgets(int win_width, int win_height) {
    if (!search_box_)
      return;

    const MapSearchOverlayLayout lo =
        map_overlay_search_layout(win_width, win_height);
    search_box_->setGeometry(lo.search_box_rect);
    if (search_results_list_) {
      search_results_list_->setGeometry(lo.results_list_rect);
    }
  }

  void hide_search_results() {
    map_search_hide_results(search_results_list_);
  }

  void show_search_results(const QJsonArray &arr) {
    if (!search_results_list_) return;

    const std::vector<MapSearchResult> parsed =
      map_search_parse_nominatim_results(arr, 8);
    const int item_count =
        map_search_populate_results(search_results_list_, parsed);

    if (item_count <= 0) {
      map_gui_push_alert(1, "No place matched your search text.");
      return;
    }

    search_results_list_->setCurrentRow(0);
    search_results_list_->setVisible(true);
    search_results_list_->raise();
    update(osm_panel_rect_);
  }

  QRect right_bottom_region() const {
    return QRect(width() / 2, height() / 2, width() - width() / 2,
                 height() - height() / 2);
  }

  QRect hover_help_invalid_rect(const QRect &anchor) const {
    if (anchor.isEmpty())
      return QRect();
    return anchor.adjusted(-320, -190, 320, 190).intersected(rect());
  }

  void apply_search_result_selection(QListWidgetItem *item) {
    if (!item) return;

    bool ok_lat = false;
    bool ok_lon = false;
    const double lat = item->data(Qt::UserRole).toDouble(&ok_lat);
    const double lon = item->data(Qt::UserRole + 1).toDouble(&ok_lon);
    if (!ok_lat || !ok_lon) return;

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
    if (search_box_) search_box_->clearFocus();
  }

  void request_visible_tiles() {
    if (QThread::currentThread() != this->thread()) {
      QMetaObject::invokeMethod(
          this, [this]() { this->request_visible_tiles(); },
          Qt::QueuedConnection);
      return;
    }
    if (!tile_net_)
      return;
    if (osm_panel_rect_.width() <= 0 || osm_panel_rect_.height() <= 0)
      return;

    MapTileRange range = map_tile_visible_range(
        osm_zoom_, osm_center_px_x_, osm_center_px_y_, osm_panel_rect_.width(),
        osm_panel_rect_.height());

    // Keep one extra tile ring warm so panning does not expose empty gaps.
    range.tx0 -= 1;
    range.tx1 += 1;
    range.ty0 -= 1;
    range.ty1 += 1;

    for (int ty = range.ty0; ty <= range.ty1; ++ty) {
      if (ty < 0 || ty >= range.n)
        continue;
      for (int tx = range.tx0; tx <= range.tx1; ++tx) {
        int tx_wrap = map_tile_wrap_x(tx, range.n);
        QString key = map_tile_key(osm_zoom_, tx_wrap, ty, dark_map_mode_);
        if (tile_cache_.find(key) != tile_cache_.end())
          continue;
        if (tile_pending_.find(key) != tile_pending_.end())
          continue;

        tile_pending_.insert(key);
        QString url = map_tile_url(osm_zoom_, tx_wrap, ty, dark_map_mode_);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");
        QNetworkReply *reply = tile_net_->get(req);
        reply->setProperty("tile_key", key);
      }
    }
  }

  void onTileReply(QNetworkReply *reply) {
    if (!reply)
      return;

    // ======== 替換開始：直接從標籤拿 key，不再解析 URL ========
    QString key = reply->property("tile_key").toString();
    // ======== 替換結束 =================================== ========
    bool tile_inserted = false;

    if (!key.isEmpty()) {
      tile_pending_.erase(key);
      if (reply->error() == QNetworkReply::NoError) {
        QPixmap px;
        QByteArray bytes = reply->readAll();
        
        // 注意這裡：把原本的 loadFromData(bytes, "PNG") 
        // 改成 loadFromData(bytes)，讓 Qt 自動判斷是 JPG 還是 PNG
        if (px.loadFromData(bytes)) {
          map_tile_store_cache_item(&tile_cache_, &tile_cache_order_, key,
                                    std::move(px));
          tile_inserted = true;
          osm_bg_needs_redraw_ = true;
        }
      }
    }

    reply->deleteLater();
    request_visible_tiles();
    if (tile_inserted && !osm_panel_rect_.isEmpty())
      update(osm_panel_rect_);
  }

  void set_selected_llh_from_point(const QPoint &pos) {
    double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
    double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
    double local_x = (double)(pos.x() - osm_panel_rect_.x());
    double local_y = (double)(pos.y() - osm_panel_rect_.y());
    double world_x = left + local_x;
    double world_y = top + local_y;

    selected_lat_deg_ = osm_world_y_to_lat(world_y, osm_zoom_);
    selected_lon_deg_ = osm_world_x_to_lon(world_x, osm_zoom_);
    selected_h_m_ = 0.0;
    has_selected_llh_ = true;

    g_receiver_lat_deg = selected_lat_deg_;
    g_receiver_lon_deg = selected_lon_deg_;
    g_receiver_valid = 1;

    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      g_ctrl.llh_ready = true;
    }

    {
      std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
      g_llh_pick_lat_deg = selected_lat_deg_;
      g_llh_pick_lon_deg = selected_lon_deg_;
      g_llh_pick_h_m = selected_h_m_;
    }
    g_gui_llh_pick_req.fetch_add(1);
  }

  bool panel_point_to_llh(const QPoint &pos, double *lat_deg,
                          double *lon_deg) const;
  void normalize_osm_center();
  void start_route_prefetch(double lat0, double lon0, double lat1,
                            double lon1);
  void onRouteReply(QNetworkReply *reply);
  bool get_current_plan_anchor(double *lat_deg, double *lon_deg);
  void set_preview_target(const QPoint &pos, int mode);
  void confirm_preview_segment();
  void try_undo_last_segment();
  bool lonlat_to_osm_screen(double lat_deg, double lon_deg, const QRect &panel,
                            QPoint *out) const;
  void draw_osm_panel(QPainter &p, const QRect &panel);

  void draw_tutorial_overlay(QPainter &p, int win_width, int win_height) {
    bool running_ui = false;
    bool detailed = false;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      running_ui = g_ctrl.running_ui;
      detailed = g_ctrl.show_detailed_ctrl;
    }

    TutorialOverlayInput overlay_in;
    overlay_in.win_width = win_width;
    overlay_in.win_height = win_height;
    overlay_in.overlay_visible = tutorial_overlay_visible_;
    overlay_in.running_ui = running_ui;
    overlay_in.detailed = detailed;
    overlay_in.step = tutorial_step_;
    overlay_in.osm_panel_rect = osm_panel_rect_;
    overlay_in.osm_stop_btn_rect = osm_stop_btn_rect_;
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
    overlay_state.text_page_anchor_spotlight = tutorial_text_page_anchor_spotlight_;

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
    tutorial_text_page_anchor_spotlight_ = overlay_state.text_page_anchor_spotlight;
  }



  void onTick();

  void refresh_tick_timer() {
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
        interval_ms = 40;
      } else if (running_ui) {
        interval_ms = 60;
      }
    }

    if (!timer_->isActive() || timer_->interval() != interval_ms) {
      timer_->start(interval_ms);
    }
  }

  void draw_map_panel(QPainter &p, const QRect &map_rect);

  void draw_control_panel(QPainter &p, int win_width, int win_height) {
    MapControlPanelInput in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.time_info = time_info_;
    QString rnx_raw = in.ctrl.rinex_name[0] ? QString::fromUtf8(in.ctrl.rinex_name) : QString("N/A");
    int slash_pos = std::max(rnx_raw.lastIndexOf('/'), rnx_raw.lastIndexOf('\\'));
    in.rnx_name = (slash_pos >= 0) ? rnx_raw.mid(slash_pos + 1) : rnx_raw;
    map_draw_control_panel(p, win_width, win_height, in);
  }
  
  void draw_spectrum_panel(QPainter &p, int win_width, int win_height) {
    MapMonitorPanelsInput in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.spec_snap = spec_snap_;
    map_draw_spectrum_panel(p, win_width, win_height, in);
  }

  void update_waterfall_image() {
    map_update_waterfall_image(width(), height(), spec_snap_, &wf_.image,
                               &wf_.width, &wf_.height);
  }

  void draw_waterfall_panel(QPainter &p, int win_width, int win_height) {
    MapMonitorPanelsInput in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.waterfall_image = wf_.image;
    in.spec_snap = spec_snap_;
    map_draw_waterfall_panel(p, win_width, win_height, in);
  }

  void draw_time_panel(QPainter &p, int win_width, int win_height) {
    MapMonitorPanelsInput in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.spec_snap = spec_snap_;
    map_draw_time_panel(p, win_width, win_height, in);
  }

  void draw_constellation_panel(QPainter &p, int win_width, int win_height) {
    MapMonitorPanelsInput in;
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      in.ctrl = g_ctrl;
    }
    in.spec_snap = spec_snap_;
    map_draw_constellation_panel(p, win_width, win_height, in);
  }

private:
  std::vector<std::vector<LonLat>> shp_parts_;
  bool shp_ok_ = false;
  std::vector<SatPoint> sats_;
  TimeInfo time_info_{};
  SpectrumSnapshot spec_snap_{};
  WaterfallState wf_{};
  QString stat_text_;
  QTimer *timer_ = nullptr;
  std::chrono::steady_clock::time_point next_time_tick_;
  std::chrono::steady_clock::time_point next_scene_tick_;
  uint64_t last_spec_seq_ = 0;
  QImage map_panel_static_bg_;
  QSize map_panel_static_bg_size_;
  QNetworkAccessManager *tile_net_ = nullptr;
  QNetworkAccessManager *route_net_ = nullptr;
  std::unordered_map<QString, QPixmap> tile_cache_;
  std::vector<QString> tile_cache_order_;
  std::set<QString> tile_pending_;
  std::unordered_map<QString, std::vector<LonLat>> route_prefetch_cache_;
  std::set<QString> route_prefetch_pending_;
  std::vector<QString> route_prefetch_order_;
  QLineEdit *inline_editor_ = nullptr;
  int inline_edit_field_ = -1;
  QRect osm_panel_rect_;
  int osm_zoom_ = 12;
  int osm_zoom_base_ = 12;
  double osm_center_px_x_ = osm_lon_to_world_x(121.5654, 12);
  double osm_center_px_y_ = osm_lat_to_world_y(25.0330, 12);
  QImage osm_bg_cache_;
  QSize osm_bg_cache_size_;
  int osm_bg_cache_zoom_ = -1;
  double osm_bg_cache_center_px_x_ = -1.0;
  double osm_bg_cache_center_px_y_ = -1.0;
  bool osm_bg_cache_dark_map_mode_ = false;
  bool osm_bg_cache_valid_ = false;
  bool dragging_osm_ = false;
  bool drag_moved_osm_ = false;
  int active_control_slider_ = -1;
  bool suppress_left_click_release_ = false;
  QPoint drag_last_pos_;
  bool has_selected_llh_ = false;
  double selected_lat_deg_ = 0.0;
  double selected_lon_deg_ = 0.0;
  double selected_h_m_ = 0.0;
  bool has_preview_segment_ = false;
  int preview_mode_ = PATH_MODE_PLAN;
  double preview_start_lat_deg_ = 0.0;
  double preview_start_lon_deg_ = 0.0;
  double preview_end_lat_deg_ = 0.0;
  double preview_end_lon_deg_ = 0.0;
  std::vector<LonLat> preview_polyline_;
  QString preview_route_key_;
  QRect back_btn_rect_;
  std::string plan_status_;

  // --- 新增：DJI 禁航區管理器與狀態 ---
  DjiNfzManager *dji_nfz_mgr_ = nullptr;
  QRect nfz_btn_rect_;
  bool osm_bg_needs_redraw_ = false;

  // 輔助函式：通知 DJI NFZ 模組地圖視角改變了
  bool is_jam_map_locked() const {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    return g_ctrl.interference_selection == 1;
  }

  void notify_nfz_viewport_changed() {
    if (!dji_nfz_mgr_ || !dji_nfz_mgr_->is_enabled() ||
        osm_panel_rect_.width() == 0)
      return;
    double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
    double right = left + (double)osm_panel_rect_.width();
    double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
    double bottom = top + (double)osm_panel_rect_.height();

    double lat_top = osm_world_y_to_lat(top, osm_zoom_);
    double lat_bot = osm_world_y_to_lat(bottom, osm_zoom_);
    double lon_left = osm_world_x_to_lon(left, osm_zoom_);
    double lon_right = osm_world_x_to_lon(right, osm_zoom_);

    dji_nfz_mgr_->trigger_fetch(lon_left, lon_right, lat_top, lat_bot,
                                osm_zoom_);
  }
  // =========================================================
  // --- 新增：搜尋功能與 Dark/Light 模式獨立變數 ---
  // =========================================================
  bool dark_map_mode_ = false;
  QRect dark_mode_btn_rect_;

  QLineEdit *search_box_ = nullptr;
  QListWidget *search_results_list_ = nullptr;
  QNetworkAccessManager *search_net_ = nullptr;
  QNetworkAccessManager *geo_net_ = nullptr;
  int search_seq_counter_ = 0;
  int latest_search_seq_ = 0;
  bool user_geo_valid_ = false;
  double user_geo_lat_deg_ = 0.0;
  double user_geo_lon_deg_ = 0.0;
  bool user_geo_bootstrap_done_ = false;
  bool user_map_interacted_ = false;
  bool show_search_return_ = false;
  double pre_search_center_x_ = 0.0;
  double pre_search_center_y_ = 0.0;
  int pre_search_zoom_ = 12;
  QRect search_return_btn_rect_;
  QRect osm_stop_btn_rect_; // <== 新增：用來記錄 OSM 上 STOP 按鈕的位置
  QRect osm_runtime_rect_;
  QRect tutorial_toggle_rect_;
  QRect tutorial_prev_btn_rect_;
  QRect tutorial_next_btn_rect_;
  QRect tutorial_close_btn_rect_;
  bool tutorial_enabled_ = false;       // default off
  bool tutorial_overlay_visible_ = false;
  int tutorial_step_ = 0;
  int tutorial_anim_step_anchor_ = -1;
  std::chrono::steady_clock::time_point tutorial_anim_start_tp_;
  int tutorial_spotlight_index_ = 0;
  int tutorial_text_page_ = 0;
  int tutorial_text_page_count_ = 1;
  int tutorial_text_page_anchor_step_ = -1;
  int tutorial_text_page_anchor_spotlight_ = -1;
  int hover_region_id_ = -1;
  bool hover_help_visible_ = false;
  QString hover_help_text_;
  QRect hover_help_anchor_rect_;
  bool map_panel_bootstrap_redraw_done_ = false;
  std::chrono::steady_clock::time_point hover_enter_tp_;

  // 獨立輔助函數：瞬間跳轉並釘上目標座標點
  void set_selected_llh_direct(double lat_deg, double lon_deg, double h_m = 0.0,
                               bool recenter_map = true) {
    selected_lat_deg_ = lat_deg;
    selected_lon_deg_ = lon_deg;
    selected_h_m_ = h_m;
    has_selected_llh_ = true;
    g_receiver_lat_deg = selected_lat_deg_;
    g_receiver_lon_deg = selected_lon_deg_;
    g_receiver_valid = 1;

    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      g_ctrl.llh_ready = true;
    }
    {
      std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
      g_llh_pick_lat_deg = selected_lat_deg_;
      g_llh_pick_lon_deg = selected_lon_deg_;
      g_llh_pick_h_m = selected_h_m_;
    }
    g_gui_llh_pick_req.fetch_add(1);

    if (recenter_map) {
      osm_center_px_x_ = osm_lon_to_world_x(lon_deg, osm_zoom_);
      osm_center_px_y_ = osm_lat_to_world_y(lat_deg, osm_zoom_);
      normalize_osm_center();
      request_visible_tiles();
    }
  }
  // =========================================================
};

#include "main_gui_path_methods.inl"

#include "main_gui_widget_methods.inl"

static void gui_thread_main() {
  int argc = 1;
  char app_name[] = "bds-sim";
  char *argv[] = {app_name, nullptr};

  QApplication app(argc, argv);
  g_app = &app;

  MapWidget w;
  QScreen *target_screen = app.primaryScreen();
  const QList<QScreen *> screens = ordered_screens_left_to_right(app.screens());
  int requested = g_gui_screen_index.load();

  fprintf(stderr, "[gui] detected screens (ordered left-to-right):\n");
  for (int i = 0; i < screens.size(); ++i) {
    QScreen *screen = screens.at(i);
    const QRect geo = screen ? screen->availableGeometry() : QRect();
    const QString name = screen ? screen->name() : QString();
    const QByteArray name_bytes = name.toUtf8();
    fprintf(stderr, "[gui]   %d: %s  geom=%dx%d+%d+%d\n",
            i + 1,
            name_bytes.constData(),
            geo.width(), geo.height(), geo.x(), geo.y());
  }

  if (!screens.isEmpty() && requested > 0) {
    int idx = requested - 1;
    if (idx >= screens.size()) idx = screens.size() - 1;
    if (idx >= 0) target_screen = screens.at(idx);
  }

  if (target_screen) {
    QRect geo = target_screen->availableGeometry();
    int w_width = std::max(1024, (int)std::lround(geo.width() * 0.80));
    int w_height = std::max(768, (int)std::lround(geo.height() * 0.80));
    int x = geo.x() + (geo.width() - w_width) / 2;
    int y = geo.y() + (geo.height() - w_height) / 2;
    w.setGeometry(x, y, w_width, w_height);
  }
  w.show();
  if (target_screen && w.windowHandle()) {
    w.windowHandle()->setScreen(target_screen);
  }

  app.exec();

  g_active_widget = nullptr;
  g_app = nullptr;
  g_running.store(false);
}

} // namespace

#include "main_gui_c_api.inl"
