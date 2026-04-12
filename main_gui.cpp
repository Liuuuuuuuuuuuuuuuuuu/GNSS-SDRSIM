#include "main_gui.h"
#include "gui/layout/geometry/control_layout.h"
#include "gui/control/panel/control_logic.h"
#include "gui/core/state/control_state.h"
#include "gui/nfz/dji_nfz.h"
#include "gui/nfz/dji_detect.h"
#include "gui/geo/geo_io.h"
#include "gui/core/runtime/gui_screen_utils.h"
#include "gui/map/render/map_fallback_land.h"
#include "gui/map/overlay/map_overlay_utils.h"
#include "gui/map/overlay/map_hover_utils.h"
#include "gui/map/overlay/crossbow_overlay.h"
#include "gui/layout/panels/map_control_panel_utils.h"
#include "gui/layout/panels/map_monitor_panels_utils.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/core/i18n/gui_font_manager.h"
#include "gui/core/runtime/gui_language_runtime_utils.h"
#include "gui/map/osm/map_osm_interaction_utils.h"
#include "gui/map/osm/map_osm_panel_utils.h"
#include "gui/map/render/map_render_utils.h"
#include "gui/map/render/map_sat_render_utils.h"
#include "gui/map/route/map_route_utils.h"
#include "gui/map/search/map_search_utils.h"
#include "gui/map/search/map_search_ui_utils.h"
#include "gui/map/osm/map_tile_utils.h"
#include "gui/nfz/nfz_hit_test_utils.h"
#include "gui/geo/osm_projection.h"
#include "gui/path/path_builder.h"
#include "gui/layout/geometry/quad_panel_layout.h"
#include "gui/core/runtime/rf_mode_utils.h"
#include "gui/core/state/signal_snapshot.h"
#include "gui/tutorial/interaction/tutorial_interaction_utils.h"
#include "gui/tutorial/overlay/tutorial_overlay_utils.h"
#include "gui/tutorial/flow/tutorial_flow_utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListWidget>
#include <QListWidgetItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <QWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

static bool has_loaded_navigation_data() {
  for (int prn = 0; prn < MAX_SAT; ++prn) {
    if (eph[prn].prn != 0) {
      return true;
    }
  }
  for (int prn = 0; prn < GPS_EPH_SLOTS; ++prn) {
    if (gps_eph[prn].prn != 0) {
      return true;
    }
  }
  return false;
}

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

    auto *fullscreen_shortcut =
        new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullscreen_shortcut->setContext(Qt::ApplicationShortcut);
    connect(fullscreen_shortcut, &QShortcut::activated, this,
            toggle_fullscreen);

    auto *exit_fullscreen_shortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), this);
    exit_fullscreen_shortcut->setContext(Qt::ApplicationShortcut);
    connect(exit_fullscreen_shortcut, &QShortcut::activated, this,
            exit_fullscreen);

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
    receiver_anim_last_tp_ = now;
    next_receiver_draw_tick_ = now;

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
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
      this->update(this->osm_panel_rect_);
    });

    // ======== 新增這兩行來預設開啟 NFZ ========
    dji_nfz_mgr_->set_enabled(true);

    dji_detect_mgr_ = new DjiDetectManager(this, [this](const DjiDetectStatus &st) {
      dji_detect_ui_detected_ = st.detected;
      dji_detect_ui_confidence_ = st.confidence;
      dji_detect_ui_model_ = st.model;
      dji_detect_ui_source_ = st.source;
      dji_detect_ui_target_count_ = st.target_count;
      {
        std::lock_guard<std::mutex> lk(g_ctrl_mtx);
        g_ctrl.crossbow_dji_detected = st.detected;
        g_ctrl.crossbow_dji_confidence = st.confidence;
      }
    });

    // 新增：設定啟動後 0.5 秒自動抓取當下視角的禁航區
    QTimer::singleShot(500, this,
                       [this]() { this->notify_nfz_viewport_changed(); });
    // ======== =========================== ========
    // =========================================================
    // --- 新增：Search Box 初始化與事件綁定 ---
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
    search_results_list_->setHorizontalScrollBarPolicy(
        Qt::ScrollBarAlwaysOff);
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

              const bool from_suggest = reply->property("search_suggest").toBool();
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
    // =========================================================

    gui_runtime_load_bundled_fonts();
    gui_font_set_zh_kai_enabled(font_zh_kai_);
    gui_runtime_apply_optional_times_fonts(font_times_bold_, font_times_italic_, font_times_bold_italic_);
    gui_runtime_apply_language_font(this, ui_language_);
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

  void set_selected_llh_centered_public(double lat_deg, double lon_deg,
                                        double h_m) {
    if (QThread::currentThread() != this->thread()) {
      QMetaObject::invokeMethod(
          this,
          [this, lat_deg, lon_deg, h_m]() {
            this->set_selected_llh_centered_public(lat_deg, lon_deg, h_m);
          },
          Qt::QueuedConnection);
      return;
    }
    set_selected_llh_direct(lat_deg, lon_deg, h_m, true);
    notify_nfz_viewport_changed();
    update(osm_panel_rect_);
  }

protected:
  void paintEvent(QPaintEvent *event) override;

  void resizeEvent(QResizeEvent *event) override;

  void mousePressEvent(QMouseEvent *event) override;

  void mouseDoubleClickEvent(QMouseEvent *event) override;

  void mouseMoveEvent(QMouseEvent *event) override;

  void leaveEvent(QEvent *event) override {
    if (tutorial_toggle_hovered_) {
      tutorial_toggle_hovered_ = false;
      if (!tutorial_toggle_rect_.isNull())
        update(tutorial_toggle_rect_.adjusted(-12, -12, 12, 12));
    }
    // 重置面板懸停狀態
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      if (g_ctrl.hover_lb_panel || g_ctrl.hover_rb_panel) {
        g_ctrl.hover_lb_panel = false;
        g_ctrl.hover_rb_panel = false;
        
        int win_width = width();
        int win_height = height();
        QRect bottom_right_rect(win_width / 2, win_height / 2,
                                win_width - win_width / 2,
                                win_height - win_height / 2);
        update(bottom_right_rect);
      }
    }
    
    QWidget::leaveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override;

  void wheelEvent(QWheelEvent *event) override;

  void keyPressEvent(QKeyEvent *event) override;

  void closeEvent(QCloseEvent *event) override;

  void begin_inline_edit(int field_id);
  void commit_inline_edit(bool apply);
  bool current_alert(QString *out_text, int *out_level) const;
  void update_alert_overlay();

private:
  QString tr_text(const char *key) const {
    return gui_runtime_text(ui_language_, key);
  }

  void update_overlay_widget_visibility();
  void layout_overlay_widgets(int win_width, int win_height);
  void hide_search_results();
  void show_search_results(const QJsonArray &arr, const QString &query,
                           bool from_suggest);
  QRect right_bottom_region() const;
  void apply_search_result_selection(QListWidgetItem *item);
  void issue_place_search(const QString &query, bool from_suggest);

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

    auto enqueue_range = [&](int tx0, int tx1, int ty0, int ty1) {
      for (int ty = ty0; ty <= ty1; ++ty) {
        if (ty < 0 || ty >= range.n)
          continue;
        for (int tx = tx0; tx <= tx1; ++tx) {
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
    };

    // Prioritize strictly visible tiles first for faster perceived fill.
    enqueue_range(range.tx0, range.tx1, range.ty0, range.ty1);

    // Warm one extra ring only when viewport is not tiny.
    const int panel_area = osm_panel_rect_.width() * osm_panel_rect_.height();
    if (panel_area >= 220000) {
      enqueue_range(range.tx0 - 1, range.tx1 + 1, range.ty0 - 1, range.ty1 + 1);
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

  void draw_tutorial_overlay(QPainter &p, int win_width, int win_height);

  void update_receiver_animation(std::chrono::steady_clock::time_point now,
                                 bool running_ui, bool *moved);



  void onTick();

  void refresh_tick_timer();

  void draw_map_panel(QPainter &p, const QRect &map_rect);

  void draw_control_panel(QPainter &p, int win_width, int win_height);
  void open_control_style_dialog();
  void update_waterfall_image();
  void draw_spectrum_panel(QPainter &p, int win_width, int win_height);
  void draw_waterfall_panel(QPainter &p, int win_width, int win_height);
  void draw_time_panel(QPainter &p, int win_width, int win_height);
  void draw_constellation_panel(QPainter &p, int win_width, int win_height);

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
  int map_wheel_delta_accum_ = 0;
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

  bool is_map_center_locked() const {
    if (is_jam_map_locked()) {
      return true;
    }
    return crossbow_center_locked_;
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
  GuiLanguage ui_language_ = GuiLanguage::English;
  double control_text_scale_ = 1.0;          // Master = 100%
  double control_caption_scale_ = 0.75;      // Caption = 75%
  double control_switch_option_scale_ = 1.50; // Switch option = 150%
  double control_value_scale_ = 1.0;
  QColor control_accent_color_ = QColor("#00e5ff");
  QColor control_border_color_ = QColor("#b9cadf");
  QColor control_text_color_ = QColor("#f8fbff");
  QColor control_dim_text_color_ = QColor("#6b7b90");
  QRect dark_mode_btn_rect_;
  QRect lang_btn_rect_;
  QRect control_gear_btn_rect_;
  bool font_times_bold_ = true;
  bool font_times_italic_ = true;
  bool font_times_bold_italic_ = true;
  bool font_zh_kai_ = false;

  QLineEdit *search_box_ = nullptr;
  QListWidget *search_results_list_ = nullptr;
  QNetworkAccessManager *search_net_ = nullptr;
  QNetworkAccessManager *geo_net_ = nullptr;
  QTimer *search_suggest_timer_ = nullptr;
  QString pending_search_query_;
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
  QRect osm_stop_btn_rect_;
  QRect osm_launch_btn_rect_;
  QRect crossbow_stage_launch_btn_rect_;
  struct CrossbowWhitelistHit {
    QRect btn_rect;
    QString device_id;
    bool currently_whitelisted = false;
  };
  std::vector<CrossbowWhitelistHit> crossbow_whitelist_hit_rows_;
  QRect crossbow_whitelist_clear_btn_rect_;
  QRect crossbow_sort_btn_rect_;
  QRect crossbow_page_prev_btn_rect_;
  QRect crossbow_page_next_btn_rect_;
  int crossbow_stage_sort_mode_ = 0; /* 0=distance, 1=threat, 2=latest */
  int crossbow_stage_page_ = 0;
  int crossbow_stage_total_pages_ = 1;
  QRect osm_runtime_rect_;
  QRect osm_scale_bar_rect_;
  std::vector<QRect> osm_status_badge_rects_;
  std::vector<QRect> osm_nfz_legend_row_rects_;
  bool scale_ruler_enabled_ = false;
  bool scale_ruler_has_start_ = false;
  double scale_ruler_start_lat_deg_ = 0.0;
  double scale_ruler_start_lon_deg_ = 0.0;
  bool scale_ruler_has_end_ = false;
  double scale_ruler_end_lat_deg_ = 0.0;
  double scale_ruler_end_lon_deg_ = 0.0;
  bool scale_ruler_end_fixed_ = false;
  std::array<bool, 4> nfz_layer_visible_ = {true, false, false, false};
  QRect tutorial_toggle_rect_;
  QRect tutorial_prev_btn_rect_;
  QRect tutorial_next_btn_rect_;
  QRect tutorial_close_btn_rect_;
  QRect tutorial_toc_btn_rects_[5];
  int tutorial_toc_btn_targets_[5] = {1, 3, 4, 5, 7};
  QRect tutorial_contents_btn_rect_;
  std::vector<QRectF> tutorial_callout_hit_boxes_;
  std::vector<QPointF> tutorial_callout_hit_anchors_;
  bool tutorial_has_glow_ = false;
  QPointF tutorial_glow_anchor_;
  int tutorial_glow_step_ = -1;
  std::chrono::steady_clock::time_point tutorial_glow_start_tp_;
  bool tutorial_toggle_hovered_ = false;
  QLabel *alert_overlay_ = nullptr;
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
  bool receiver_anim_valid_ = false;
  double receiver_anim_lat_deg_ = 0.0;
  double receiver_anim_lon_deg_ = 0.0;
  double receiver_anim_target_lat_deg_ = 0.0;
  double receiver_anim_target_lon_deg_ = 0.0;
  std::chrono::steady_clock::time_point receiver_anim_last_tp_;
  std::chrono::steady_clock::time_point next_receiver_draw_tick_;

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
  // --- 新增：DJI 偵測管理器與 Crossbow 狀態機成員 ---
  // =========================================================
  DjiDetectManager *dji_detect_mgr_ = nullptr;

  // Crossbow 自動干擾（spoofing/jamming）狀態機
  bool crossbow_auto_jam_latched_ = false;
  bool crossbow_spoof_zone_latched_ = false;
  bool crossbow_landing_hint_alerted_ = false;
  bool crossbow_spoof_following_ = false;
  bool crossbow_spoof_following_alerted_ = false;
  long long crossbow_start_ms_ = 0LL;
  bool crossbow_direction_locked_ = false;
  double crossbow_locked_bearing_deg_ = 0.0;
  double crossbow_beam_bearing_deg_ = 0.0;
  double crossbow_auto_jam_trigger_m_ = 100.0;
  bool crossbow_cached_nfz_valid_ = false;
  bool crossbow_has_mouse_ = false;
  QPoint crossbow_mouse_pos_;
  bool crossbow_auto_overview_done_ = false;
  bool crossbow_far_fetch_done_ = false;
  double crossbow_cached_nfz_bearing_deg_ = 0.0;
  double crossbow_cached_nfz_lat_deg_ = 0.0;
  double crossbow_cached_nfz_lon_deg_ = 0.0;
  double crossbow_cached_nfz_dist_m_ = 0.0;
  bool crossbow_center_locked_ = false;
  double crossbow_center_lock_lat_deg_ = 0.0;
  double crossbow_center_lock_lon_deg_ = 0.0;
  std::chrono::steady_clock::time_point crossbow_oor_hold_until_tp_;
  double crossbow_oor_hold_dist_m_ = 0.0;

  // DJI 偵測 UI 緩衝狀態
  bool dji_detect_ui_detected_ = false;
  double dji_detect_ui_confidence_ = 0.0;
  QString dji_detect_ui_model_;
  QString dji_detect_ui_source_;
  int dji_detect_ui_target_count_ = 0;
  int last_interference_selection_ = -1;  // 用於偵測狀態模式切換

  // --- 輔助函式 ---
  bool is_crossbow_mode() const {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    return g_ctrl.interference_selection == 2;
  }

public:
  // --- 公開橋接方法（提供 C API 轉接） ---
  void set_dji_detect_status_public(int detected, double confidence) {
    if (dji_detect_mgr_) {
      dji_detect_mgr_->set_manual_override(true, detected != 0, confidence, "C API override");
    }
  }

  void set_dji_whitelist_csv_public(const QString &csv) {
    if (dji_detect_mgr_) {
      dji_detect_mgr_->set_whitelist_csv(csv);
    }
  }

  void auto_zoom_for_location_public(double lat_deg, double lon_deg) {
    /* Auto-zoom for 50m ring visibility (same logic as Crossbow entry) */
    const double cos_lat_az = std::max(0.15, std::cos(lat_deg * M_PI / 180.0));
    const double az_target_mpp = 50.0 / 120.0;  /* 50m ring occupies ~120px */
    const double az_zoom_f = std::log2((40075016.0 * cos_lat_az) / (256.0 * az_target_mpp));
    const int az_zoom = std::max(15, std::min(20, (int)std::llround(az_zoom_f)));
    if (osm_zoom_ < az_zoom) {
      osm_zoom_ = az_zoom;
      osm_center_px_x_ = osm_lon_to_world_x(lon_deg, osm_zoom_);
      osm_center_px_y_ = osm_lat_to_world_y(lat_deg, osm_zoom_);
      normalize_osm_center();
      request_visible_tiles();
    }
  }

  void* get_dji_detect_manager_public() {
    return dji_detect_mgr_;
  }
  // =========================================================

  // Crossbow 相關輔助方法（狀態機重設）
  void reset_crossbow_direction_state() {
    crossbow_has_mouse_ = false;
    crossbow_mouse_pos_ = QPoint();
    crossbow_auto_overview_done_ = false;
    crossbow_direction_locked_ = false;
    crossbow_locked_bearing_deg_ = 0.0;
    crossbow_cached_nfz_valid_ = false;
    crossbow_far_fetch_done_ = false;
    crossbow_oor_hold_dist_m_ = 0.0;
    crossbow_oor_hold_until_tp_ = std::chrono::steady_clock::time_point{};
  }

  void sync_crossbow_ctrl_flags(bool direction_confirmed, bool distance_ok) {
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.crossbow_direction_confirmed = direction_confirmed;
    g_ctrl.crossbow_distance_ok = distance_ok;
  }

  void sync_receiver_marker_to_selected_llh() {
    const double snapped_lon = wrap_lon_deg(selected_lon_deg_);
    const auto now = std::chrono::steady_clock::now();

    g_receiver_lat_deg = selected_lat_deg_;
    g_receiver_lon_deg = snapped_lon;
    g_receiver_valid = 1;

    receiver_anim_valid_ = true;
    receiver_anim_lat_deg_ = selected_lat_deg_;
    receiver_anim_lon_deg_ = snapped_lon;
    receiver_anim_target_lat_deg_ = selected_lat_deg_;
    receiver_anim_target_lon_deg_ = snapped_lon;
    receiver_anim_last_tp_ = now;
    next_receiver_draw_tick_ = now;
  }

  friend class MapWidgetInitMethods;
};

#include "gui/core/widget/map_widget_ui_methods.inl"
#include "gui/tutorial/widget/map_widget_tutorial_methods.inl"
#include "gui/control/widget/map_widget_control_methods.inl"

#include "main_gui_render_event_methods.inl"
#include "main_gui_input_event_methods.inl"

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
  w.showFullScreen();
  if (target_screen && w.windowHandle()) {
    w.windowHandle()->setScreen(target_screen);
    w.showFullScreen();
  }

  app.exec();

  g_active_widget = nullptr;
  g_app = nullptr;
  g_running.store(false);
}

} // namespace

#include "main_gui_c_api.inl"
