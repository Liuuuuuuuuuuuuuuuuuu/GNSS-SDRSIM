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
#include "gui/core/gui_i18n.h"
#include "gui/core/gui_font_manager.h"
#include "gui/core/gui_language_runtime_utils.h"
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
        if (dirty_intersects(bottom_right_rect)) {
          MapMonitorPanelsInput in;
          {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            in.ctrl = g_ctrl;
          }
          in.spec_snap = spec_snap_;
          in.waterfall_image = wf_.image;
          in.language = ui_language_;

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

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    layout_overlay_widgets(width(), height());
    if (alert_overlay_) {
      alert_overlay_->setGeometry(rect());
    }
    update_alert_overlay();
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
      const bool tutorial_was_visible = tutorial_overlay_visible_;

      // TOC jump: when on the Contents page (step 0), check TOC buttons first
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

      // CONTENTS button (steps 1-7): jump back to TOC
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

      // Callout click → glow animation at UI element anchor
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
        open_control_style_dialog();
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
      rects.osm_stop_btn_rect = osm_stop_btn_rect_;
      rects.dark_mode_btn_rect = dark_mode_btn_rect_;
      rects.search_return_btn_rect = search_return_btn_rect_;
      rects.nfz_btn_rect = nfz_btn_rect_;
      rects.back_btn_rect = back_btn_rect_;
      rects.nfz_legend_row_rects = osm_nfz_legend_row_rects_;
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
    const bool tutorial_hovered =
        !tutorial_toggle_rect_.isNull() && tutorial_toggle_rect_.contains(event->pos());
    if (tutorial_toggle_hovered_ != tutorial_hovered) {
      tutorial_toggle_hovered_ = tutorial_hovered;
      update(tutorial_toggle_rect_.adjusted(-12, -12, 12, 12));
    }

    // 檢測滑鼠是否懸停在展開面板上
    {
      std::lock_guard<std::mutex> lk(g_ctrl_mtx);
      
      if (g_ctrl.running_ui) {
        int win_width = width();
        int win_height = height();
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        
        // 計算左下面板區域（包括上下兩部分）
        get_rb_lq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                      &panel_w, &panel_h, false, 1.0);
        QRect lb_panel_upper(panel_x, panel_y, panel_w, panel_h);
        
        get_rb_lq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                      &panel_w, &panel_h, true, 1.0);
        QRect lb_panel_lower(panel_x, panel_y, panel_w, panel_h);
        
        QRect lb_combined = lb_panel_upper.united(lb_panel_lower);
        bool hover_lb = lb_combined.contains(event->pos());
        
        // 計算右下面板區域（包括上下兩部分）
        get_rb_rq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                      &panel_w, &panel_h, false, 1.0);
        QRect rb_panel_upper(panel_x, panel_y, panel_w, panel_h);
        
        get_rb_rq_panel_rect_expanded(win_width, win_height, &panel_x, &panel_y,
                                      &panel_w, &panel_h, true, 1.0);
        QRect rb_panel_lower(panel_x, panel_y, panel_w, panel_h);
        
        QRect rb_combined = rb_panel_upper.united(rb_panel_lower);
        bool hover_rb = rb_combined.contains(event->pos());
        
        // 檢查是否有變化，如果有則更新並請求重繪
        bool hover_changed = (g_ctrl.hover_lb_panel != hover_lb) || (g_ctrl.hover_rb_panel != hover_rb);
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

    if (map_osm_handle_move(
            event->pos(), event->buttons(), osm_panel_rect_,
            is_jam_map_locked(), &dragging_osm_, &drag_moved_osm_,
            &drag_last_pos_, &osm_center_px_x_, &osm_center_px_y_,
          nullptr,
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
      dragging_osm_ = false;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    QPoint p0 = event->position().toPoint();

    // Tutorial page navigation by mouse wheel.
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

    // Keep wheel scrolling inside search results from leaking into map zoom.
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

  void keyPressEvent(QKeyEvent *event) override {
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
  QRect osm_stop_btn_rect_; // <== 新增：用來記錄 OSM 上 STOP 按鈕的位置
  QRect osm_runtime_rect_;
  std::vector<QRect> osm_status_badge_rects_;
  std::vector<QRect> osm_nfz_legend_row_rects_;
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

#include "gui/core/map_widget_ui_methods.inl"
#include "gui/tutorial/map_widget_tutorial_methods.inl"
#include "gui/control/map_widget_control_methods.inl"

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
