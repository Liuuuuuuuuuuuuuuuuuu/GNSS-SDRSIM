#include "map_gui.h"
#include "gui/geo_io.h"
#include "gui/path_builder.h"
#include "gui/signal_snapshot.h"
#include "gui/control_layout.h"
#include "gui/control_logic.h"
#include "gui/control_paint.h"
#include "gui/control_state.h"
#include "gui/map_render_utils.h"
#include "gui/osm_projection.h"
#include "gui/quad_panel_layout.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFont>
#include <QImage>
#include <QLineEdit>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRect>
#include <QRegion>
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

static const LonLat kPolyNorthAmerica[] = {
    {-168, 72}, {-150, 70}, {-140, 60}, {-130, 55}, {-124, 48}, {-123, 40},
    {-117, 32}, {-108, 26}, {-97, 20}, {-88, 18}, {-82, 24}, {-81, 30},
    {-90, 45}, {-110, 60}, {-135, 70}, {-160, 74}
};

static const LonLat kPolySouthAmerica[] = {
    {-81, 12}, {-75, 7}, {-70, -5}, {-66, -15}, {-64, -25}, {-62, -35},
    {-58, -45}, {-52, -54}, {-45, -52}, {-41, -40}, {-38, -25}, {-46, -8},
    {-55, 2}, {-67, 10}
};

static const LonLat kPolyEurasia[] = {
    {-10, 35}, {0, 45}, {20, 55}, {40, 60}, {60, 62}, {80, 65}, {100, 62},
    {125, 58}, {145, 50}, {160, 48}, {170, 60}, {175, 68}, {150, 72},
    {120, 72}, {90, 70}, {65, 68}, {45, 60}, {30, 50}, {20, 40}, {12, 35},
    {5, 38}, {-5, 40}
};

static const LonLat kPolyAfrica[] = {
    {-17, 37}, {-6, 35}, {10, 35}, {22, 32}, {33, 25}, {41, 12}, {44, 3},
    {40, -8}, {34, -20}, {27, -30}, {15, -35}, {5, -34}, {-3, -22},
    {-9, -5}, {-15, 10}
};

static const LonLat kPolyAustralia[] = {
    {112, -11}, {123, -16}, {134, -18}, {146, -22}, {153, -29}, {150, -38},
    {141, -40}, {130, -35}, {118, -30}, {113, -22}
};

static const LonLat kPolyGreenland[] = {
    {-73, 60}, {-60, 65}, {-48, 70}, {-40, 76}, {-42, 82}, {-55, 84},
    {-65, 80}, {-70, 72}
};


enum {
    PATH_SEG_QUEUED = 0,
    PATH_SEG_EXECUTING = 1
};


static constexpr int kMaxQueuedSegments = 5;

struct GuiPathSegment {
    double start_lat_deg;
    double start_lon_deg;
    double end_lat_deg;
    double end_lon_deg;
    int mode;
    int state;
    std::vector<LonLat> polyline;
    char path_file[256];
};

std::mutex g_path_seg_mtx;
std::vector<GuiPathSegment> g_path_segments;

std::atomic<bool> g_running(false);
std::thread g_gui_thread;
QApplication *g_app = nullptr;

double g_start_bdt = 0.0;
std::chrono::steady_clock::time_point g_start_tp;
std::mutex g_time_mtx;

std::mutex g_ctrl_mtx;
GuiControlState g_ctrl = {
    100.0,
    1.0,
    FS_OUTPUT_HZ / 1e6,
    CN0_TARGET_DBHZ,
    72.0,
    2.0,
    1,
    0,
    2,
    16,
    {0},
    0,
    0,
    false,
    false,
    false,
    true,
    true,
    false,
    false,
    false,
    "N/A"
};

std::atomic<uint32_t> g_gui_start_req(0);
std::atomic<uint32_t> g_gui_stop_req(0);
std::atomic<uint32_t> g_gui_exit_req(0);
std::atomic<uint32_t> g_gui_llh_pick_req(0);
std::mutex g_llh_pick_mtx;
double g_llh_pick_lat_deg = 0.0;
double g_llh_pick_lon_deg = 0.0;
double g_llh_pick_h_m = 0.0;


struct WaterfallState {
    QImage image;
    int width;
    int height;
};

static constexpr int kMonitorInnerPadX = 10;
static constexpr int kMonitorInnerPadTop = 24;
static constexpr int kMonitorInnerPadBottom = 18;

static inline double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline double mode_min_fs_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_MIN_FS_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_MIN_FS_HZ;
    return RF_BDS_ONLY_MIN_FS_HZ;
}

static inline double mode_default_fs_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_MIN_FS_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_MIN_FS_HZ;
    return RF_BDS_ONLY_MIN_FS_HZ;
}

static inline double mode_tx_center_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_CENTER_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_CENTER_HZ;
    return RF_BDS_ONLY_CENTER_HZ;
}

static inline double snap_fs_to_mode_grid_hz(double fs_hz, uint8_t signal_mode)
{
    const double step = RF_FS_STEP_HZ;
    const double min_fs = mode_min_fs_hz(signal_mode);
    if (fs_hz < min_fs) fs_hz = min_fs;

    double k = std::round(fs_hz / step);
    if (k < 1.0) k = 1.0;
    fs_hz = k * step;

    if (fs_hz < min_fs) fs_hz = min_fs;
    return fs_hz;
}

static bool handle_control_click(int x, int y, int win_width, int win_height)
{
    return control_logic_handle_click(x,
                                      y,
                                      win_width,
                                      win_height,
                                      &g_ctrl,
                                      &g_ctrl_mtx,
                                      &g_gui_start_req,
                                      &g_gui_stop_req,
                                      &g_gui_exit_req,
                                      &g_runtime_abort);
}

static bool control_value_text_for_field(int field_id, char *out, size_t out_sz)
{
    return control_logic_value_text_for_field(field_id, out, out_sz, &g_ctrl, &g_ctrl_mtx);
}

static bool handle_control_value_input(int field_id, const char *input)
{
    return control_logic_handle_value_input(field_id, input, &g_ctrl, &g_ctrl_mtx);
}

static bool handle_control_slider_drag(int slider_id, int x, int win_width, int win_height)
{
    return control_logic_handle_slider_drag(slider_id, x, win_width, win_height, &g_ctrl, &g_ctrl_mtx);
}

class MapWidget final : public QWidget {
public:
    MapWidget()
    {
        setWindowTitle("GNSS Subpoint Map");
        setMouseTracking(false);

        shp_ok_ = load_land_shapefile("./ne_50m_land/ne_50m_land.shp", shp_parts_);
        build_time_info(&time_info_);
        fetch_spectrum_snapshot(&spec_snap_);
        wf_.width = 0;
        wf_.height = 0;
        stat_text_ = "Satellites: 0";

        auto now = std::chrono::steady_clock::now();
        next_time_tick_ = now;
        next_scene_tick_ = now;

        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);
        connect(timer_, &QTimer::timeout, this, [this]() { this->onTick(); });
        timer_->start(20);

        tile_net_ = new QNetworkAccessManager(this);
        connect(tile_net_, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
            this->onTileReply(reply);
        });

        route_net_ = new QNetworkAccessManager(this);
        connect(route_net_, &QNetworkAccessManager::finished, this, [this](QNetworkReply *reply) {
            this->onRouteReply(reply);
        });

        inline_editor_ = new QLineEdit(this);
        inline_editor_->hide();
        inline_editor_->setAlignment(Qt::AlignCenter);
        inline_editor_->setMaxLength(32);
        inline_editor_->setStyleSheet(
            "QLineEdit {"
            "background:#0f1b2b;"
            "color:#f8fbff;"
            "border:1px solid #b9cadf;"
            "border-radius:3px;"
            "padding:0 2px;"
            "}");
        connect(inline_editor_, &QLineEdit::returnPressed, this, [this]() { this->commit_inline_edit(true); });
        connect(inline_editor_, &QLineEdit::editingFinished, this, [this]() {
            if (inline_editor_ && inline_editor_->isVisible()) this->commit_inline_edit(true);
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QLinearGradient scene_grad(rect().topLeft(), rect().bottomRight());
        scene_grad.setColorAt(0.0, QColor("#030712"));
        scene_grad.setColorAt(0.45, QColor("#0b1b31"));
        scene_grad.setColorAt(1.0, QColor("#05101f"));
        p.fillRect(rect(), scene_grad);

        int win_width = width();
        int win_height = height();

        QRect osm_rect(0, 0, win_width / 2, (win_height * 3) / 4);
        draw_osm_panel(p, osm_rect);

        QRect map_rect(win_width / 2, 0, win_width - win_width / 2, win_height / 2);
        draw_map_panel(p, map_rect);
        draw_control_panel(p, win_width, win_height);
        draw_spectrum_panel(p, win_width, win_height);
        draw_waterfall_panel(p, win_width, win_height);
        draw_time_panel(p, win_width, win_height);
        draw_constellation_panel(p, win_width, win_height);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (inline_editor_ && inline_editor_->isVisible() && !inline_editor_->geometry().contains(event->pos())) {
                commit_inline_edit(true);
            }

            int value_field = control_value_hit_test(event->pos().x(), event->pos().y(), width(), height());
            if (value_field >= 0) {
                begin_inline_edit(value_field);
                event->accept();
                return;
            }

            int slider_id = control_slider_hit_test(event->pos().x(), event->pos().y(), width(), height());
            if (slider_id >= 0) {
                active_control_slider_ = slider_id;
                if (handle_control_slider_drag(active_control_slider_, event->pos().x(), width(), height())) {
                    update();
                }
                event->accept();
                return;
            }
            if (osm_panel_rect_.contains(event->pos())) {
                bool running_ui = false;
                {
                    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
                    running_ui = g_ctrl.running_ui;
                }
                if (running_ui && back_btn_rect_.contains(event->pos())) {
                    try_undo_last_segment();
                    update(osm_panel_rect_);
                    event->accept();
                    return;
                }
                dragging_osm_ = true;
                drag_moved_osm_ = false;
                drag_last_pos_ = event->pos();
                event->accept();
                return;
            }
            if (handle_control_click(event->pos().x(), event->pos().y(), width(), height())) {
                update();
            }
        } else if (event->button() == Qt::RightButton) {
            bool running_ui = false;
            {
                std::lock_guard<std::mutex> lk(g_ctrl_mtx);
                running_ui = g_ctrl.running_ui;
            }
            if (running_ui && osm_panel_rect_.contains(event->pos())) {
                confirm_preview_segment();
                update(osm_panel_rect_);
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && osm_panel_rect_.contains(event->pos())) {
            bool running_ui = false;
            {
                std::lock_guard<std::mutex> lk(g_ctrl_mtx);
                running_ui = g_ctrl.running_ui;
            }
            if (running_ui) {
                set_preview_target(event->pos(), PATH_MODE_PLAN);
                suppress_left_click_release_ = true;
                update(osm_panel_rect_);
                event->accept();
                return;
            }
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (active_control_slider_ >= 0 && (event->buttons() & Qt::LeftButton)) {
            if (handle_control_slider_drag(active_control_slider_, event->pos().x(), width(), height())) {
                update();
            }
            event->accept();
            return;
        }

        if (dragging_osm_ && (event->buttons() & Qt::LeftButton) && osm_panel_rect_.contains(event->pos())) {
            QPoint delta = event->pos() - drag_last_pos_;
            if (std::abs(delta.x()) + std::abs(delta.y()) >= 2) {
                drag_moved_osm_ = true;
            }
            drag_last_pos_ = event->pos();
            osm_center_px_x_ -= delta.x();
            osm_center_px_y_ -= delta.y();
            normalize_osm_center();
            request_visible_tiles();
            update(osm_panel_rect_);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            active_control_slider_ = -1;
            if (suppress_left_click_release_) {
                suppress_left_click_release_ = false;
                dragging_osm_ = false;
                event->accept();
                return;
            }
            if (dragging_osm_ && osm_panel_rect_.contains(event->pos()) && !drag_moved_osm_) {
                bool running_ui = false;
                {
                    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
                    running_ui = g_ctrl.running_ui;
                }
                if (running_ui) {
                    set_preview_target(event->pos(), PATH_MODE_LINE);
                    update(osm_panel_rect_);
                } else {
                    set_selected_llh_from_point(event->pos());
                    update(osm_panel_rect_);
                }
            }
            dragging_osm_ = false;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        QPoint p0 = event->position().toPoint();
        if (!osm_panel_rect_.contains(p0)) {
            QWidget::wheelEvent(event);
            return;
        }

        int delta = event->angleDelta().y();
        if (delta == 0) {
            event->accept();
            return;
        }

        int old_zoom = osm_zoom_;
        int new_zoom = clamp_int(old_zoom + (delta > 0 ? 1 : -1), 2, 18);
        if (new_zoom == old_zoom) {
            event->accept();
            return;
        }

        double old_world = osm_world_size_for_zoom(old_zoom);
        double new_world = osm_world_size_for_zoom(new_zoom);
        double scale = new_world / old_world;

        double vp_x = (double)(p0.x() - osm_panel_rect_.x());
        double vp_y = (double)(p0.y() - osm_panel_rect_.y());
        double old_left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
        double old_top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
        double world_x = old_left + vp_x;
        double world_y = old_top + vp_y;

        osm_zoom_ = new_zoom;
        osm_center_px_x_ *= scale;
        osm_center_px_y_ *= scale;

        double new_world_x = world_x * scale;
        double new_world_y = world_y * scale;
        double new_left = new_world_x - vp_x;
        double new_top = new_world_y - vp_y;
        osm_center_px_x_ = new_left + (double)osm_panel_rect_.width() * 0.5;
        osm_center_px_y_ = new_top + (double)osm_panel_rect_.height() * 0.5;

        normalize_osm_center();
        request_visible_tiles();
        update(osm_panel_rect_);
        event->accept();
    }

    void closeEvent(QCloseEvent *event) override
    {
        commit_inline_edit(true);
        g_running.store(false);
        event->accept();
        QWidget::closeEvent(event);
    }

    void begin_inline_edit(int field_id)
    {
        if (!inline_editor_) return;

        char val[64] = {0};
        if (!control_value_text_for_field(field_id, val, sizeof(val))) return;

        ControlLayout lo;
        compute_control_layout(width(), height(), &lo);

        Rect sr = lo.tx_slider;
        switch (field_id) {
        case CTRL_SLIDER_TX: sr = lo.tx_slider; break;
        case CTRL_SLIDER_GAIN: sr = lo.gain_slider; break;
        case CTRL_SLIDER_FS: sr = lo.fs_slider; break;
        case CTRL_SLIDER_CN0: sr = lo.cn0_slider; break;
        case CTRL_SLIDER_SEED: sr = lo.seed_slider; break;
        case CTRL_SLIDER_PRN: sr = lo.prn_slider; break;
        case CTRL_SLIDER_PATH_V: sr = lo.path_v_slider; break;
        case CTRL_SLIDER_PATH_A: sr = lo.path_a_slider; break;
        case CTRL_SLIDER_CH: sr = lo.ch_slider; break;
        default: return;
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

    void commit_inline_edit(bool apply)
    {
        if (!inline_editor_ || inline_edit_field_ < 0) return;

        int field = inline_edit_field_;
        inline_edit_field_ = -1;

        bool changed = false;
        if (apply) {
            QByteArray ba = inline_editor_->text().trimmed().toUtf8();
            changed = handle_control_value_input(field, ba.constData());
        }

        inline_editor_->hide();
        if (changed) update();
    }

private:
    void normalize_osm_center()
    {
        osm_normalize_center(osm_zoom_, &osm_center_px_x_, &osm_center_px_y_);
    }

    QString tile_key(int z, int x, int y) const
    {
        return QString::number(z) + "/" + QString::number(x) + "/" + QString::number(y);
    }

    void request_visible_tiles()
    {
        if (osm_panel_rect_.width() < 8 || osm_panel_rect_.height() < 8 || !tile_net_) return;

        int n = 1 << osm_zoom_;
        double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
        double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
        double right = left + (double)osm_panel_rect_.width();
        double bottom = top + (double)osm_panel_rect_.height();

        int tx0 = (int)std::floor(left / 256.0);
        int tx1 = (int)std::floor(right / 256.0);
        int ty0 = (int)std::floor(top / 256.0);
        int ty1 = (int)std::floor(bottom / 256.0);

        for (int ty = ty0; ty <= ty1; ++ty) {
            if (ty < 0 || ty >= n) continue;
            for (int tx = tx0; tx <= tx1; ++tx) {
                int tx_wrap = tx % n;
                if (tx_wrap < 0) tx_wrap += n;
                QString key = tile_key(osm_zoom_, tx_wrap, ty);
                if (tile_cache_.find(key) != tile_cache_.end()) continue;
                if (tile_pending_.find(key) != tile_pending_.end()) continue;

                tile_pending_.insert(key);
                QString url = QString("https://tile.openstreetmap.org/%1/%2/%3.png")
                                  .arg(osm_zoom_)
                                  .arg(tx_wrap)
                                  .arg(ty);
                QNetworkRequest req{QUrl(url)};
                req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");
                tile_net_->get(req);
            }
        }
    }

    void onTileReply(QNetworkReply *reply)
    {
        if (!reply) return;

        QUrl u = reply->url();
        QString path = u.path();
        QStringList seg = path.split('/', Qt::SkipEmptyParts);
        QString key;
        if (seg.size() >= 3) {
            QString z = seg[seg.size() - 3];
            QString x = seg[seg.size() - 2];
            QString y = seg[seg.size() - 1];
            if (y.endsWith(".png")) y.chop(4);
            key = z + "/" + x + "/" + y;
        }

        if (!key.isEmpty()) {
            tile_pending_.erase(key);
            if (reply->error() == QNetworkReply::NoError) {
                QPixmap px;
                QByteArray bytes = reply->readAll();
                if (px.loadFromData(bytes, "PNG")) {
                    tile_cache_[key] = px;
                }
            }
        }

        reply->deleteLater();
        update(osm_panel_rect_);
    }

    void set_selected_llh_from_point(const QPoint &pos)
    {
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

    bool panel_point_to_llh(const QPoint &pos, double *lat_deg, double *lon_deg) const
    {
        if (!osm_panel_rect_.contains(pos)) return false;

        double left = osm_center_px_x_ - (double)osm_panel_rect_.width() * 0.5;
        double top = osm_center_px_y_ - (double)osm_panel_rect_.height() * 0.5;
        double local_x = (double)(pos.x() - osm_panel_rect_.x());
        double local_y = (double)(pos.y() - osm_panel_rect_.y());
        double world_x = left + local_x;
        double world_y = top + local_y;

        if (lat_deg) *lat_deg = osm_world_y_to_lat(world_y, osm_zoom_);
        if (lon_deg) *lon_deg = osm_world_x_to_lon(world_x, osm_zoom_);
        return true;
    }

    QString make_route_cache_key(double lat0, double lon0, double lat1, double lon1) const
    {
        return QString("%1,%2|%3,%4")
            .arg(lat0, 0, 'f', 6)
            .arg(wrap_lon_deg(lon0), 0, 'f', 6)
            .arg(lat1, 0, 'f', 6)
            .arg(wrap_lon_deg(lon1), 0, 'f', 6);
    }

    void maybe_trim_route_cache()
    {
        static constexpr int kMaxRouteCache = 96;
        if ((int)route_prefetch_order_.size() <= kMaxRouteCache) return;

        int drop_n = (int)route_prefetch_order_.size() - kMaxRouteCache;
        for (int i = 0; i < drop_n; ++i) {
            const QString &old_key = route_prefetch_order_[i];
            route_prefetch_cache_.erase(old_key);
        }
        route_prefetch_order_.erase(route_prefetch_order_.begin(), route_prefetch_order_.begin() + drop_n);
    }

    void start_route_prefetch(double lat0, double lon0, double lat1, double lon1)
    {
        if (!route_net_) return;

        QString key = make_route_cache_key(lat0, lon0, lat1, lon1);
        if (route_prefetch_cache_.find(key) != route_prefetch_cache_.end()) return;
        if (route_prefetch_pending_.find(key) != route_prefetch_pending_.end()) return;

        QString url = QString("https://router.project-osrm.org/route/v1/driving/%1,%2;%3,%4?overview=full&geometries=geojson")
                          .arg(lon0, 0, 'f', 7)
                          .arg(lat0, 0, 'f', 7)
                          .arg(lon1, 0, 'f', 7)
                          .arg(lat1, 0, 'f', 7);

        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");

        QNetworkReply *reply = route_net_->get(req);
        reply->setProperty("route_key", key);
        route_prefetch_pending_.insert(key);
    }

    void onRouteReply(QNetworkReply *reply)
    {
        if (!reply) return;

        QString key = reply->property("route_key").toString();
        bool route_ready = false;
        if (!key.isEmpty()) {
            route_prefetch_pending_.erase(key);
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray body = reply->readAll();
                QJsonParseError perr;
                QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
                if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject root = doc.object();
                    QJsonArray routes = root.value("routes").toArray();
                    if (!routes.isEmpty() && routes.at(0).isObject()) {
                        QJsonObject route0 = routes.at(0).toObject();
                        QJsonObject geom = route0.value("geometry").toObject();
                        QJsonArray coords = geom.value("coordinates").toArray();
                        if (coords.size() >= 2) {
                            std::vector<LonLat> polyline;
                            polyline.reserve((size_t)coords.size());
                            for (int i = 0; i < coords.size(); ++i) {
                                QJsonArray pt = coords.at(i).toArray();
                                if (pt.size() < 2) continue;
                                polyline.push_back({pt.at(0).toDouble(), pt.at(1).toDouble()});
                            }
                            if (polyline.size() >= 2) {
                                route_prefetch_cache_[key] = std::move(polyline);
                                route_prefetch_order_.push_back(key);
                                maybe_trim_route_cache();
                                route_ready = true;
                            }
                        }
                    }
                }
            }

            if (key == preview_route_key_) {
                if (route_ready) {
                    auto it = route_prefetch_cache_.find(key);
                    if (it != route_prefetch_cache_.end() && it->second.size() >= 2) {
                        preview_polyline_ = it->second;
                        has_preview_segment_ = true;
                        plan_status_ = "Road preview ready: right-click to confirm";
                    }
                } else {
                    has_preview_segment_ = false;
                    plan_status_ = "Road routing failed: no drivable map road path";
                }
                update(osm_panel_rect_);
            }
        }

        reply->deleteLater();
    }

    bool get_current_plan_anchor(double *lat_deg, double *lon_deg)
    {
        std::lock_guard<std::mutex> lk(g_path_seg_mtx);
        if (!g_path_segments.empty()) {
            const GuiPathSegment &last = g_path_segments.back();
            if (lat_deg) *lat_deg = last.end_lat_deg;
            if (lon_deg) *lon_deg = last.end_lon_deg;
            return true;
        }

        if (g_receiver_valid) {
            if (lat_deg) *lat_deg = g_receiver_lat_deg;
            if (lon_deg) *lon_deg = g_receiver_lon_deg;
            return true;
        }

        if (has_selected_llh_) {
            if (lat_deg) *lat_deg = selected_lat_deg_;
            if (lon_deg) *lon_deg = selected_lon_deg_;
            return true;
        }
        return false;
    }

    void set_preview_target(const QPoint &pos, int mode)
    {
        double lat = 0.0;
        double lon = 0.0;
        if (!panel_point_to_llh(pos, &lat, &lon)) return;

        double slat = 0.0;
        double slon = 0.0;
        if (!get_current_plan_anchor(&slat, &slon)) {
            plan_status_ = "Path planning needs a valid start anchor";
            return;
        }

        preview_start_lat_deg_ = slat;
        preview_start_lon_deg_ = slon;
        preview_end_lat_deg_ = lat;
        preview_end_lon_deg_ = lon;
        preview_mode_ = mode;
        preview_polyline_.clear();
        preview_route_key_.clear();

        if (distance_m_approx(slat, slon, lat, lon) <= 0.5) {
            has_preview_segment_ = false;
            plan_status_ = "Preview point too close to start";
            return;
        }

        if (mode == PATH_MODE_LINE) {
            preview_polyline_.push_back({wrap_lon_deg(slon), slat});
            preview_polyline_.push_back({wrap_lon_deg(lon), lat});
            has_preview_segment_ = true;
            plan_status_ = "Line preview ready: right-click to confirm";
            start_route_prefetch(slat, slon, lat, lon);
            return;
        }

        {
            QString key = make_route_cache_key(slat, slon, lat, lon);
            preview_route_key_ = key;
            auto it = route_prefetch_cache_.find(key);
            if (it != route_prefetch_cache_.end() && it->second.size() >= 2) {
                preview_polyline_ = it->second;
                has_preview_segment_ = true;
                plan_status_ = "Road preview ready (cached): right-click to confirm";
                return;
            }

            start_route_prefetch(slat, slon, lat, lon);
            has_preview_segment_ = false;
            plan_status_ = "Road preview loading...";
            return;
        }
    }

    void confirm_preview_segment()
    {
        if (!has_preview_segment_) {
            plan_status_ = "No preview path to confirm";
            return;
        }

        {
            std::lock_guard<std::mutex> lk(g_path_seg_mtx);
            if ((int)g_path_segments.size() >= kMaxQueuedSegments) {
                plan_status_ = "Path queue is full (max 5)";
                return;
            }
        }

        char file_path[256] = {0};
        std::vector<LonLat> seg_polyline;
        double vmax_mps = 20.0;
        double accel_mps2 = 2.0;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            vmax_mps = g_ctrl.path_vmax_kmh / 3.6;
            accel_mps2 = g_ctrl.path_accel_mps2;
        }
        if (!build_segment_path_file(preview_start_lat_deg_, preview_start_lon_deg_,
                                     preview_end_lat_deg_, preview_end_lon_deg_,
                                     preview_mode_, vmax_mps, accel_mps2, &preview_polyline_, file_path,
                                     &seg_polyline)) {
            plan_status_ = "Failed to build path file from preview";
            return;
        }

        if (!enqueue_path_file_name(file_path)) {
            plan_status_ = "Queue rejected path (max 5 active segments)";
            return;
        }

        GuiPathSegment seg{};
        seg.start_lat_deg = preview_start_lat_deg_;
        seg.start_lon_deg = preview_start_lon_deg_;
        seg.end_lat_deg = preview_end_lat_deg_;
        seg.end_lon_deg = preview_end_lon_deg_;
        seg.mode = preview_mode_;
        seg.state = PATH_SEG_QUEUED;
        seg.polyline = std::move(seg_polyline);
        std::snprintf(seg.path_file, sizeof(seg.path_file), "%s", file_path);

        {
            std::lock_guard<std::mutex> lk(g_path_seg_mtx);
            g_path_segments.push_back(seg);
        }

        has_preview_segment_ = false;
        preview_polyline_.clear();
        plan_status_ = "Segment confirmed and queued";
    }

    void try_undo_last_segment()
    {
        char removed[256] = {0};
        if (!delete_last_queued_path(removed, sizeof(removed))) {
            plan_status_ = "Cannot undo: last segment is already executing or queue is empty";
            return;
        }

        map_gui_notify_path_segment_undo();
        has_preview_segment_ = false;
        preview_polyline_.clear();
        plan_status_ = "Last queued segment has been undone";
    }

    bool lonlat_to_osm_screen(double lat_deg, double lon_deg, const QRect &panel, QPoint *out) const
    {
        if (!out) return false;
        double world = osm_world_size_for_zoom(osm_zoom_);
        double left = osm_center_px_x_ - (double)panel.width() * 0.5;
        double top = osm_center_px_y_ - (double)panel.height() * 0.5;
        double wx0 = osm_lon_to_world_x(lon_deg, osm_zoom_);
        double wy = osm_lat_to_world_y(lat_deg, osm_zoom_);

        int best_sx = 0;
        int best_sy = 0;
        double best_d2 = 1e30;
        bool ok = false;
        for (int k = -1; k <= 1; ++k) {
            double wx = wx0 + (double)k * world;
            int sx = panel.x() + (int)llround(wx - left);
            int sy = panel.y() + (int)llround(wy - top);
            double dx = (double)sx - (double)(panel.x() + panel.width() / 2);
            double dy = (double)sy - (double)(panel.y() + panel.height() / 2);
            double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                best_sx = sx;
                best_sy = sy;
                ok = true;
            }
        }
        if (ok) *out = QPoint(best_sx, best_sy);
        return ok;
    }

    void draw_osm_panel(QPainter &p, const QRect &panel)
    {
        osm_panel_rect_ = panel;
        QColor color_bg_top("#0e2239");
        QColor color_bg_bottom("#081425");
        QColor color_border("#c6d4e6");
        QColor color_text("#f1f7ff");
        QColor color_subtle_text("#9db4cf");

        QRect shell = panel.adjusted(0, 0, -1, -1);
        QLinearGradient shell_grad(shell.topLeft(), shell.bottomLeft());
        shell_grad.setColorAt(0.0, color_bg_top);
        shell_grad.setColorAt(1.0, color_bg_bottom);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(shell_grad);
        p.drawRoundedRect(shell, 10, 10);
        p.setRenderHint(QPainter::Antialiasing, false);
        if (panel.width() < 8 || panel.height() < 8) return;

        request_visible_tiles();

        int n = 1 << osm_zoom_;
        double left = osm_center_px_x_ - (double)panel.width() * 0.5;
        double top = osm_center_px_y_ - (double)panel.height() * 0.5;
        int tx0 = (int)std::floor(left / 256.0);
        int tx1 = (int)std::floor((left + panel.width()) / 256.0);
        int ty0 = (int)std::floor(top / 256.0);
        int ty1 = (int)std::floor((top + panel.height()) / 256.0);

        for (int ty = ty0; ty <= ty1; ++ty) {
            if (ty < 0 || ty >= n) continue;
            for (int tx = tx0; tx <= tx1; ++tx) {
                int tx_wrap = tx % n;
                if (tx_wrap < 0) tx_wrap += n;

                double world_x = (double)tx * 256.0;
                double world_y = (double)ty * 256.0;
                int sx = panel.x() + (int)llround(world_x - left);
                int sy = panel.y() + (int)llround(world_y - top);

                QString key = tile_key(osm_zoom_, tx_wrap, ty);
                auto it = tile_cache_.find(key);
                if (it != tile_cache_.end()) {
                    p.drawPixmap(sx, sy, 256, 256, it->second);
                } else {
                    p.fillRect(QRect(sx, sy, 256, 256), QColor(14, 32, 54));
                    p.setPen(QPen(QColor(74, 101, 132), 1));
                    p.drawRect(sx, sy, 255, 255);
                }
            }
        }

        p.setPen(QPen(color_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 10, 10);

        if (has_selected_llh_) {
            double world = osm_world_size_for_zoom(osm_zoom_);
            double left = osm_center_px_x_ - (double)panel.width() * 0.5;
            double top = osm_center_px_y_ - (double)panel.height() * 0.5;
            double wx0 = osm_lon_to_world_x(selected_lon_deg_, osm_zoom_);
            double wy = osm_lat_to_world_y(selected_lat_deg_, osm_zoom_);

            int best_sx = 0;
            int best_sy = 0;
            double best_dist = 1e30;
            bool visible = false;
            for (int k = -1; k <= 1; ++k) {
                double wx = wx0 + (double)k * world;
                int sx = panel.x() + (int)llround(wx - left);
                int sy = panel.y() + (int)llround(wy - top);
                if (sx >= panel.x() - 12 && sx <= panel.x() + panel.width() + 12 &&
                    sy >= panel.y() - 12 && sy <= panel.y() + panel.height() + 12) {
                    visible = true;
                    double dx = (double)sx - (panel.x() + panel.width() * 0.5);
                    double dy = (double)sy - (panel.y() + panel.height() * 0.5);
                    double d2 = dx * dx + dy * dy;
                    if (d2 < best_dist) {
                        best_dist = d2;
                        best_sx = sx;
                        best_sy = sy;
                    }
                }
            }

            if (visible) {
                QColor marker("#ff3b30");
                QColor marker_outline("#111111");
                p.setPen(QPen(marker_outline, 2));
                p.setBrush(marker);
                p.drawEllipse(QPoint(best_sx, best_sy), 6, 6);
                p.drawLine(best_sx - 10, best_sy, best_sx + 10, best_sy);
                p.drawLine(best_sx, best_sy - 10, best_sx, best_sy + 10);
            }
        }

        std::vector<GuiPathSegment> segs;
        {
            std::lock_guard<std::mutex> lk(g_path_seg_mtx);
            segs = g_path_segments;
        }

        for (const auto &seg : segs) {
            QColor c = (seg.state == PATH_SEG_EXECUTING) ? QColor("#f59e0b") : QColor("#22c55e");
            p.setPen(QPen(c, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);

            QPainterPath path;
            bool has_path = false;
            if (seg.polyline.size() >= 2) {
                for (size_t i = 0; i < seg.polyline.size(); ++i) {
                    QPoint pt;
                    if (!lonlat_to_osm_screen(seg.polyline[i].lat, seg.polyline[i].lon, panel, &pt)) continue;
                    if (!has_path) {
                        path.moveTo(pt);
                        has_path = true;
                    } else {
                        path.lineTo(pt);
                    }
                }
            }
            if (!has_path) {
                QPoint a, b;
                if (lonlat_to_osm_screen(seg.start_lat_deg, seg.start_lon_deg, panel, &a) &&
                    lonlat_to_osm_screen(seg.end_lat_deg, seg.end_lon_deg, panel, &b)) {
                    path.moveTo(a);
                    path.lineTo(b);
                    has_path = true;
                }
            }
            if (has_path) {
                p.drawPath(path);
            }

            QPoint a, b;
            if (!lonlat_to_osm_screen(seg.start_lat_deg, seg.start_lon_deg, panel, &a)) continue;
            if (!lonlat_to_osm_screen(seg.end_lat_deg, seg.end_lon_deg, panel, &b)) continue;
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(a, 6, 6);
            p.drawEllipse(b, 6, 6);
        }

        if (has_preview_segment_) {
            QPoint a, b;
            if (lonlat_to_osm_screen(preview_start_lat_deg_, preview_start_lon_deg_, panel, &a) &&
                lonlat_to_osm_screen(preview_end_lat_deg_, preview_end_lon_deg_, panel, &b)) {
                QColor c = (preview_mode_ == PATH_MODE_LINE) ? QColor("#60a5fa") : QColor("#fbbf24");
                p.setPen(QPen(c, 4, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);

                QPainterPath path;
                bool has_path = false;
                if (preview_polyline_.size() >= 2) {
                    for (size_t i = 0; i < preview_polyline_.size(); ++i) {
                        QPoint pt;
                        if (!lonlat_to_osm_screen(preview_polyline_[i].lat, preview_polyline_[i].lon, panel, &pt)) continue;
                        if (!has_path) {
                            path.moveTo(pt);
                            has_path = true;
                        } else {
                            path.lineTo(pt);
                        }
                    }
                }
                if (!has_path) {
                    path.moveTo(a);
                    path.lineTo(b);
                    has_path = true;
                }
                if (has_path) {
                    p.drawPath(path);
                }
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawEllipse(a, 5, 5);
                p.drawEllipse(b, 5, 5);
            }
        }

        if (g_receiver_valid) {
            QPoint cur;
            if (lonlat_to_osm_screen(g_receiver_lat_deg, g_receiver_lon_deg, panel, &cur)) {
                QColor cur_color("#22d3ee");
                QColor cur_outline("#0f172a");
                p.setPen(QPen(cur_outline, 2));
                p.setBrush(cur_color);
                p.drawEllipse(cur, 8, 8);
                p.drawLine(cur.x() - 12, cur.y(), cur.x() + 12, cur.y());
                p.drawLine(cur.x(), cur.y() - 12, cur.x(), cur.y() + 12);
            }
        }

        bool running_ui = false;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            running_ui = g_ctrl.running_ui;
        }
        back_btn_rect_ = QRect(panel.x() + panel.width() - 98, panel.y() + 44, 90, 26);
        if (running_ui) {
            bool can_undo = false;
            {
                std::lock_guard<std::mutex> lk(g_path_seg_mtx);
                can_undo = !g_path_segments.empty() && g_path_segments.back().state == PATH_SEG_QUEUED;
            }
            QColor fill = can_undo ? QColor("#0ea5e9") : QColor("#334155");
            QColor border = can_undo ? QColor("#e2e8f0") : QColor("#64748b");
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(border, 1));
            p.setBrush(fill);
            p.drawRoundedRect(back_btn_rect_, 4, 4);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QColor("#f8fafc"));
            p.drawText(back_btn_rect_, Qt::AlignCenter, "BACK");
        }

        p.setPen(color_text);
        p.drawText(panel.x() + 10, panel.y() + 18, "OpenStreetMap (drag to pan, wheel to zoom)");
        char zbuf[32];
        std::snprintf(zbuf, sizeof(zbuf), "Zoom %d", osm_zoom_);
        p.drawText(panel.x() + panel.width() - 86, panel.y() + 18, zbuf);

        if (has_selected_llh_) {
            char llh_buf[96];
            std::snprintf(llh_buf, sizeof(llh_buf), "Start LLH %.6f, %.6f, %.1f", selected_lat_deg_, selected_lon_deg_,
                          selected_h_m_);
            p.drawText(panel.x() + 10, panel.y() + panel.height() - 10, llh_buf);
        }

        char cur_fixed_buf[112];
        if (g_receiver_valid) {
            std::snprintf(cur_fixed_buf, sizeof(cur_fixed_buf), "Current %.6f, %.6f", g_receiver_lat_deg, g_receiver_lon_deg);
        } else {
            std::snprintf(cur_fixed_buf, sizeof(cur_fixed_buf), "Current N/A");
        }
        int cur_w = p.fontMetrics().horizontalAdvance(cur_fixed_buf);
        p.setPen(color_subtle_text);
        p.drawText(panel.x() + panel.width() - cur_w - 10, panel.y() + panel.height() - 10, cur_fixed_buf);

        if (!plan_status_.empty()) {
            p.setPen(color_subtle_text);
            p.drawText(panel.x() + 10, panel.y() + panel.height() - 28, QString::fromStdString(plan_status_));
        }
    }

    void onTick()
    {
        if (!g_running.load()) {
            close();
            return;
        }

        bool llh_ready = false;
        int signal_mode = SIG_MODE_BDS;
        bool interference_mode = false;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            llh_ready = g_ctrl.llh_ready;
            signal_mode = (int)g_ctrl.signal_mode;
            interference_mode = g_ctrl.interference_mode;
        }
        if (!llh_ready) {
            has_selected_llh_ = false;
            has_preview_segment_ = false;
            preview_polyline_.clear();
            plan_status_.clear();
        }

        bool redraw = false;
        auto now = std::chrono::steady_clock::now();

        if (now >= next_time_tick_) {
            do {
                next_time_tick_ += std::chrono::milliseconds(100);
            } while (next_time_tick_ <= now);
            build_time_info(&time_info_);
            redraw = true;
        }

        if (now >= next_scene_tick_) {
            do {
                next_scene_tick_ += std::chrono::seconds(1);
            } while (next_scene_tick_ <= now);
            compute_sat_points(sats_, time_info_.week, time_info_.sow, signal_mode);
            char buf[64];
            const char *sys_name = (signal_mode == SIG_MODE_GPS) ? "GPS" :
                                   ((signal_mode == SIG_MODE_MIXED) ? "BDS+GPS" : "Non-GEO BDS");
            if (interference_mode)
                std::snprintf(buf, sizeof(buf), "JAM %s (Rand-BPSK)", sys_name);
            else
                std::snprintf(buf, sizeof(buf), "%s Satellites: %zu", sys_name, sats_.size());
            stat_text_ = buf;
            redraw = true;
        }

        bool spec_draw_tick = false;
        pthread_mutex_lock(&g_gui_spectrum_mtx);
        uint64_t seq = g_gui_spectrum_seq;
        pthread_mutex_unlock(&g_gui_spectrum_mtx);
        if (seq != last_spec_seq_) {
            last_spec_seq_ = seq;
            spec_draw_tick = true;
            fetch_spectrum_snapshot(&spec_snap_);
            redraw = true;
        }

        if (spec_draw_tick) update_waterfall_image();
        if (redraw) update();
    }

    void draw_map_panel(QPainter &p, const QRect &map_rect)
    {
        QColor color_ocean_top("#15395d");
        QColor color_ocean_bottom("#0b1f36");
        QColor color_grid("#5f7ea0");
        QColor color_land("#8ac07a");
        QColor color_sat("#ffd166");
        QColor color_sat_dim("#5b6472");
        QColor color_sat_active("#ff4d6d");
        QColor color_sat_selected_green("#22c55e");
        QColor color_sat_outline("#111111");
        QColor color_sat_outline_dim("#2f3744");
        QColor color_rx("#00e5ff");
        QColor color_text("#f7fbff");
        QColor color_text_dim("#9aa9bc");
        QColor color_border("#c6d4e6");

        QLinearGradient map_grad(map_rect.topLeft(), map_rect.bottomLeft());
        map_grad.setColorAt(0.0, color_ocean_top);
        map_grad.setColorAt(1.0, color_ocean_bottom);
        p.fillRect(map_rect, map_grad);

        p.setPen(QPen(color_grid, 1));
        for (int lon = -180; lon <= 180; lon += 30) {
            int x = map_rect.x() + lon_to_x((double)lon, map_rect.width());
            p.drawLine(x, map_rect.y(), x, map_rect.y() + map_rect.height() - 1);
        }
        for (int lat = -60; lat <= 60; lat += 30) {
            int y = map_rect.y() + lat_to_y((double)lat, map_rect.height());
            p.drawLine(map_rect.x(), y, map_rect.x() + map_rect.width() - 1, y);
        }

        p.setPen(color_text);
        map_draw_ticks(p, map_rect);

        p.setPen(QPen(color_land, 1));
        if (shp_ok_) {
            map_draw_shp_land(p, shp_parts_, map_rect);
        } else {
            p.setBrush(color_land);
            map_draw_poly(p, kPolyNorthAmerica, (int)(sizeof(kPolyNorthAmerica) / sizeof(kPolyNorthAmerica[0])), map_rect);
            map_draw_poly(p, kPolySouthAmerica, (int)(sizeof(kPolySouthAmerica) / sizeof(kPolySouthAmerica[0])), map_rect);
            map_draw_poly(p, kPolyEurasia, (int)(sizeof(kPolyEurasia) / sizeof(kPolyEurasia[0])), map_rect);
            map_draw_poly(p, kPolyAfrica, (int)(sizeof(kPolyAfrica) / sizeof(kPolyAfrica[0])), map_rect);
            map_draw_poly(p, kPolyAustralia, (int)(sizeof(kPolyAustralia) / sizeof(kPolyAustralia[0])), map_rect);
            map_draw_poly(p, kPolyGreenland, (int)(sizeof(kPolyGreenland) / sizeof(kPolyGreenland[0])), map_rect);
            p.setBrush(Qt::NoBrush);
        }

        GuiControlState st;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            st = g_ctrl;
        }

        for (const auto &sat : sats_) {
            int x = map_rect.x() + lon_to_x(sat.lon_deg, map_rect.width());
            int y = map_rect.y() + lat_to_y(sat.lat_deg, map_rect.height());

            bool in_candidate = false;
            for (int k = 0; k < st.single_candidate_count; ++k) {
                if (st.single_candidates[k] == sat.prn) {
                    in_candidate = true;
                    break;
                }
            }

            bool label_on = true;
            if (st.sat_mode == 0) {
                label_on = in_candidate;
            } else if (st.sat_mode == 1) {
                label_on = sat.is_gps ? (sat.prn >= 1 && sat.prn <= 32) : (sat.prn >= 1 && sat.prn <= 37);
            }

            bool is_selected = (sat.prn >= 1 && sat.prn < MAX_SAT && g_active_prn_mask[sat.prn] != 0);
            QColor outline = label_on ? color_sat_outline : color_sat_outline_dim;
            QColor fill = label_on ? color_sat : color_sat_dim;

            p.setPen(Qt::NoPen);
            p.setBrush(outline);
            p.drawEllipse(QPoint(x, y), 6, 6);

            if (is_selected) {
                p.setBrush(st.running_ui ? color_sat_active : color_sat_selected_green);
            } else {
                p.setBrush(fill);
            }
            p.drawEllipse(QPoint(x, y), 4, 4);

            char label[16];
            std::snprintf(label, sizeof(label), "%c%02d", sat.is_gps ? 'G' : 'C', sat.prn);
            p.setPen(label_on ? color_text : color_text_dim);
            p.drawText(x + 9, y - 9, label);
        }

        if (g_receiver_valid) {
            int rx_x = map_rect.x() + lon_to_x(g_receiver_lon_deg, map_rect.width());
            int rx_y = map_rect.y() + lat_to_y(g_receiver_lat_deg, map_rect.height());
            p.setPen(QPen(color_rx, 2));
            p.drawLine(rx_x - 8, rx_y, rx_x + 8, rx_y);
            p.drawLine(rx_x, rx_y - 8, rx_x, rx_y + 8);
            p.setBrush(color_rx);
            p.drawEllipse(QPoint(rx_x, rx_y), 3, 3);
            p.drawText(rx_x + 10, rx_y - 10, "RX");
        }

        p.setPen(QPen(color_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(map_rect.adjusted(0, 0, -1, -1), 10, 10);
    }

    void draw_control_panel(QPainter &p, int win_width, int win_height)
    {
        QColor color_border("#b9cadf");
        QColor color_text("#f8fbff");
        QColor color_dim("#6b7b90");
        QColor color_panel_top("#0e1f34");
        QColor color_panel_bottom("#06111f");
        QColor color_title("#8bc3ff");
        QColor color_start_btn("#1fbe7b");
        QColor color_stop_btn("#ef5350");
        QColor color_start_off("#275844");
        QColor color_stop_off("#6b3a3a");
        QColor color_btn_text_black(8, 12, 18);
        QColor color_exit_on("#ffb547");
        QColor color_exit_off("#425165");

        ControlLayout lo;
        compute_control_layout(win_width, win_height, &lo);

        QRect panel_rect(lo.panel.x, lo.panel.y, lo.panel.w - 1, lo.panel.h - 1);
        QLinearGradient panel_grad(panel_rect.topLeft(), panel_rect.bottomLeft());
        panel_grad.setColorAt(0.0, color_panel_top);
        panel_grad.setColorAt(1.0, color_panel_bottom);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(panel_grad);
        p.drawRoundedRect(panel_rect, 10, 10);
        p.setRenderHint(QPainter::Antialiasing, false);

        GuiControlState st;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            st = g_ctrl;
        }

        bool ctrl_enabled = st.llh_ready && !st.running_ui;
        bool standby_enabled = !st.running_ui;
        QColor ctrl_border = ctrl_enabled ? color_border : color_dim;
        QColor ctrl_text = ctrl_enabled ? color_text : color_dim;
        QColor standby_border = standby_enabled ? color_border : color_dim;
        QColor standby_text = standby_enabled ? color_text : color_dim;

        QFont base_font = p.font();
        QFont title_font = base_font;
        title_font.setBold(true);
        title_font.setPointSize(base_font.pointSize() > 0 ? base_font.pointSize() + 1 : 11);

        p.setFont(title_font);
        p.setPen(color_title);
        p.drawText(lo.panel.x + 10, lo.panel.y + 18, "Control Panel");

        char time_line[160];
        std::snprintf(time_line, sizeof(time_line), "%s | %s", time_info_.utc_label, time_info_.bdt_label);
        p.setFont(base_font);
        p.setPen(color_text);
        p.drawText(lo.panel.x + 104, lo.panel.y + 18, time_line);

        long long elapsed_sec = 0;
        {
            std::lock_guard<std::mutex> lk(g_time_mtx);
            if (st.running_ui) {
                elapsed_sec =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_start_tp).count();
                if (elapsed_sec < 0) elapsed_sec = 0;
            }
        }

        int hh = (int)(elapsed_sec / 3600);
        int mm = (int)((elapsed_sec % 3600) / 60);
        int ss = (int)(elapsed_sec % 60);

        char elapsed_line[64];
        std::snprintf(elapsed_line, sizeof(elapsed_line), "TX Elapsed %02d:%02d:%02d", hh, mm, ss);

        char rinex_line[320];
        std::snprintf(rinex_line, sizeof(rinex_line), "RINEX %s", st.rinex_name[0] ? st.rinex_name : "N/A");

        p.setPen(st.running_ui ? color_text : color_dim);
        int right_x = lo.panel.x + lo.panel.w - 12;
        control_draw_text_right(p, right_x, lo.panel.y + 18, elapsed_line);
        control_draw_text_right(p, right_x, lo.panel.y + 34, rinex_line);

        p.setPen(color_text);
        p.drawText(lo.panel.x + 10, lo.panel.y + 34, stat_text_);

        char v_tx[32], v_gain[32], v_fs[32], v_cn0[32], v_seed[32], v_prn[64], v_path_v[32], v_path_a[32];
        std::snprintf(v_tx, sizeof(v_tx), "%.1f dB", st.tx_gain);
        std::snprintf(v_gain, sizeof(v_gain), "%.2f", st.gain);
        std::snprintf(v_fs, sizeof(v_fs), "%.2f MHz", st.fs_mhz);
        std::snprintf(v_cn0, sizeof(v_cn0), "%.1f dB-Hz", st.target_cn0);
        std::snprintf(v_seed, sizeof(v_seed), "%u", st.seed);
        std::snprintf(v_path_v, sizeof(v_path_v), "%.1f km/h", st.path_vmax_kmh);
        std::snprintf(v_path_a, sizeof(v_path_a), "%.1f m/s2", st.path_accel_mps2);
        if (st.single_candidate_count > 0) {
            std::snprintf(v_prn, sizeof(v_prn), "PRN %d", st.single_prn);
        } else {
            std::snprintf(v_prn, sizeof(v_prn), "N/A");
        }

        char v_ch[32];
        std::snprintf(v_ch, sizeof(v_ch), "%d", st.max_ch);

        double fs_hz = st.fs_mhz * 1e6;
        double fs_min = mode_min_fs_hz(st.signal_mode);
        double fs_ratio = (fs_hz - fs_min) / std::max(1.0, 31.2e6 - fs_min);
        double prn_ratio = 0.0;
        if (st.single_candidate_count > 1) {
            prn_ratio = (double)st.single_candidate_idx / (double)(st.single_candidate_count - 1);
        }

        control_draw_slider(p, lo.tx_slider, color_border, color_text, color_dim, QColor(56, 189, 248, 220),
                    "TX Gain", v_tx, st.tx_gain / 100.0, ctrl_enabled);
        control_draw_slider(p, lo.gain_slider, color_border, color_text, color_dim, QColor(96, 165, 250, 220),
                    "Signal Gain", v_gain, (st.gain - 0.1) / (20.0 - 0.1), ctrl_enabled);
        control_draw_slider(p, lo.fs_slider, color_border, color_text, color_dim, QColor(59, 130, 246, 220),
                    "Fs", v_fs, fs_ratio, ctrl_enabled);
        control_draw_slider(p, lo.cn0_slider, color_border, color_text, color_dim, QColor(14, 165, 233, 220),
                    "Target C/N0", v_cn0, (st.target_cn0 - 20.0) / 40.0, ctrl_enabled);
        control_draw_slider(p, lo.seed_slider, color_border, color_text, color_dim, QColor(52, 211, 153, 220),
                    "Seed", v_seed, ((double)st.seed - 1.0) / 999999998.0, ctrl_enabled);
        control_draw_slider(p, lo.prn_slider, color_border, color_text, color_dim, QColor(45, 212, 191, 220),
                    "Preview PRN", v_prn, prn_ratio, ctrl_enabled && st.sat_mode == 0 && st.single_candidate_count > 0);
        control_draw_slider(p, lo.path_v_slider, color_border, color_text, color_dim, QColor(248, 113, 113, 220),
                    "Path Vmax", v_path_v, (st.path_vmax_kmh - 3.6) / (288.0 - 3.6), ctrl_enabled);
        control_draw_slider(p, lo.path_a_slider, color_border, color_text, color_dim, QColor(251, 146, 60, 220),
                    "Path Acc", v_path_a, (st.path_accel_mps2 - 0.2) / (10.0 - 0.2), ctrl_enabled);
        control_draw_slider(p, lo.ch_slider, color_border, color_text, color_dim, QColor(250, 204, 21, 220),
                    "Max CH", v_ch, ((double)st.max_ch - 1.0) / 15.0, ctrl_enabled);

        int mode_idx = clamp_int(st.sat_mode, 0, 2);
        int sys_idx = (st.signal_mode == SIG_MODE_BDS) ? 0 : ((st.signal_mode == SIG_MODE_MIXED) ? 1 : 2);
        control_draw_three_switch(p, lo.sw_mode, color_border, color_text, color_dim, QColor(38, 115, 219, 220),
                  "MOD", "SINGLE", "1-37", "1-63", mode_idx, ctrl_enabled);
        control_draw_three_switch(p, lo.sw_sys, color_border, color_text, color_dim, QColor(24, 160, 126, 220),
                  "SYS", "BDS", "BDS+GPS", "GPS", sys_idx, standby_enabled);

        control_draw_checkbox(p, lo.tg_meo, ctrl_border, ctrl_text, color_dim, "MEO", st.meo_only, ctrl_enabled);
        control_draw_checkbox(p, lo.tg_iono, ctrl_border, ctrl_text, color_dim, "IONO", st.iono_on, ctrl_enabled);
        control_draw_checkbox(p, lo.tg_clk, ctrl_border, ctrl_text, color_dim, "EXT CLK", st.usrp_external_clk, ctrl_enabled);
        control_draw_two_switch(p, lo.sw_fmt, standby_border, standby_text, color_dim, QColor(56, 189, 248, 220),
                    "FMT", "SHORT", "BYTE", st.byte_output ? 1 : 0, standby_enabled);
        control_draw_two_switch(p, lo.sw_jam, ctrl_border, ctrl_text, color_dim, QColor(239, 68, 68, 220),
                    "MODE", "SPOOF", "JAM", st.interference_mode ? 1 : 0, ctrl_enabled);

        control_draw_button_filled(p, lo.btn_start,
                       st.llh_ready ? (st.running_ui ? color_start_off : color_start_btn) : color_start_off,
                       st.llh_ready ? (st.running_ui ? color_start_off : color_start_btn) : color_start_off,
                       color_btn_text_black, "START");
        control_draw_button_filled(p, lo.btn_stop,
                       st.llh_ready ? (st.running_ui ? color_stop_btn : color_stop_off) : color_stop_off,
                       st.llh_ready ? (st.running_ui ? color_stop_btn : color_stop_off) : color_stop_off,
                       color_btn_text_black, "STOP");

        bool exit_enabled = !st.running_ui;
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(exit_enabled ? color_text : color_dim);
        p.setBrush(exit_enabled ? color_exit_on : color_exit_off);
        p.drawRoundedRect(lo.btn_exit.x, lo.btn_exit.y, lo.btn_exit.w - 1, lo.btn_exit.h - 1, 3, 3);
        int pad = 4;
        p.drawLine(lo.btn_exit.x + pad, lo.btn_exit.y + pad, lo.btn_exit.x + lo.btn_exit.w - 1 - pad,
                   lo.btn_exit.y + lo.btn_exit.h - 1 - pad);
        p.drawLine(lo.btn_exit.x + lo.btn_exit.w - 1 - pad, lo.btn_exit.y + pad, lo.btn_exit.x + pad,
                   lo.btn_exit.y + lo.btn_exit.h - 1 - pad);
        p.setRenderHint(QPainter::Antialiasing, false);
        if (exit_enabled) {
            p.drawText(lo.btn_exit.x - 34, lo.btn_exit.y + lo.btn_exit.h - 4, "EXIT");
        }

        QString cand_line;
        if (st.single_candidate_count <= 0) {
            cand_line = "Visible: none (check LLH/ephemeris)";
        } else {
            cand_line = "Visible:";
            int max_w = lo.panel.w - 20;
            for (int i = 0; i < st.single_candidate_count; ++i) {
                QString trial = cand_line + QString(" %1").arg(st.single_candidates[i]);
                if (p.fontMetrics().horizontalAdvance(trial) > max_w) {
                    cand_line += " ...";
                    break;
                }
                cand_line = trial;
            }
        }

        p.setPen(ctrl_enabled ? color_text : color_dim);
        QRect cand_rect(lo.panel.x + 10, lo.btn_start.y - 13, lo.panel.w - 20, 12);
        p.drawText(cand_rect, Qt::AlignLeft | Qt::AlignVCenter, cand_line);
    }

    void draw_spectrum_panel(QPainter &p, int win_width, int win_height)
    {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
        if (panel_w < 180 || panel_h < 120) return;

        QRect panel(panel_x, panel_y, panel_w, panel_h);
        QColor color_grid("#4f6986");
        QColor color_border("#c4d2e4");
        QColor color_curve("#51a7ff");
        QColor color_title("#8fc7ff");

        QLinearGradient panel_grad(panel.topLeft(), panel.bottomLeft());
        panel_grad.setColorAt(0.0, QColor("#101f33"));
        panel_grad.setColorAt(1.0, QColor("#081423"));

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(panel_grad);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setPen(QPen(color_grid, 1));
        for (int i = 1; i < 4; ++i) {
            int yy = panel_y + (panel_h * i) / 4;
            p.drawLine(panel_x + 1, yy, panel_x + panel_w - 2, yy);
        }
        for (int i = 1; i < 6; ++i) {
            int xx = panel_x + (panel_w * i) / 6;
            p.drawLine(xx, panel_y + 1, xx, panel_y + panel_h - 2);
        }

        if (spec_snap_.valid && spec_snap_.bins >= 8) {
            int x0 = panel_x + 6;
            int y0 = panel_y + panel_h - 8;
            int draw_w = panel_w - 12;
            int draw_h = panel_h - 20;
            int usable_h = (draw_h * 3) / 4;
            if (usable_h < 4) usable_h = draw_h;

            p.setPen(QPen(color_curve, 1));
            QPainterPath path;
            for (int i = 0; i < spec_snap_.bins; ++i) {
                float t = (float)i / (float)(spec_snap_.bins - 1);
                int x = x0 + (int)llround(t * (float)(draw_w - 1));
                float norm = (spec_snap_.rel_db[i] + 30.0f) / 30.0f;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                int y = y0 - (int)llround(norm * (float)(usable_h - 1));
                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);

            p.setPen(QPen(QColor(81, 167, 255, 96), 3));
            p.drawPath(path);
        }

        p.setPen(color_border);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setPen(color_title);
        p.drawText(panel_x + 8, panel_y + 16, "Signal Spectrum");

        const int db_max = 0;
        const int db_mid = -15;
        const int db_min = -30;
        char db_top[24], db_mid_s[24], db_bot[24];
        std::snprintf(db_top, sizeof(db_top), "%d dB", db_max);
        std::snprintf(db_mid_s, sizeof(db_mid_s), "%d dB", db_mid);
        std::snprintf(db_bot, sizeof(db_bot), "%d dB", db_min);
        p.setPen(color_border);
        p.drawText(panel_x + 8, panel_y + 48, db_top);
        p.drawText(panel_x + 8, panel_y + panel_h / 2, db_mid_s);
        p.drawText(panel_x + 8, panel_y + panel_h - 18, db_bot);

        GuiControlState st;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            st = g_ctrl;
        }
        const double cf_mhz = mode_tx_center_hz(st.signal_mode) / 1e6;
        const double fs_mhz = (st.fs_mhz > 0.0) ? st.fs_mhz : (mode_default_fs_hz(st.signal_mode) / 1e6);
        const double half_bw_mhz = fs_mhz * 0.5;

        char info[120];
        std::snprintf(info, sizeof(info), "CF %.3f MHz  Fs %.3f MHz  FFT %d  BINS %d  %dfps", cf_mhz,
                      fs_mhz, GUI_SPEC_FFT_POINTS, GUI_SPECTRUM_BINS, GUI_SPEC_FPS);
        p.drawText(panel_x + 8, panel_y + 32, info);

        double f_l = cf_mhz - half_bw_mhz;
        double f_c = cf_mhz;
        double f_r = cf_mhz + half_bw_mhz;
        char lbl_l[24], lbl_c[24], lbl_r[24];
        std::snprintf(lbl_l, sizeof(lbl_l), "%.3f", f_l);
        std::snprintf(lbl_c, sizeof(lbl_c), "%.3f", f_c);
        std::snprintf(lbl_r, sizeof(lbl_r), "%.3f", f_r);
        int y_lbl = panel_y + panel_h - 4;
        p.drawText(panel_x + 6, y_lbl, lbl_l);
        p.drawText(panel_x + panel_w / 2 - 18, y_lbl, lbl_c);
        p.drawText(panel_x + panel_w - 74, y_lbl, lbl_r);
    }

    void update_waterfall_image()
    {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_lq_panel_rect(width(), height(), &panel_x, &panel_y, &panel_w, &panel_h, true);
        if (panel_w < 180 || panel_h < 120) return;

        int draw_w = panel_w - 2 * kMonitorInnerPadX;
        int draw_h = panel_h - (kMonitorInnerPadTop + kMonitorInnerPadBottom);
        if (draw_w < 8 || draw_h < 8) return;

        if (wf_.width != draw_w || wf_.height != draw_h || wf_.image.isNull()) {
            wf_.image = QImage(draw_w, draw_h, QImage::Format_RGB32);
            wf_.image.fill(Qt::black);
            wf_.width = draw_w;
            wf_.height = draw_h;
        }

        if (!spec_snap_.valid || spec_snap_.bins < 8) return;

        if (draw_h > 1) {
            for (int y = draw_h - 1; y > 0; --y) {
                std::memcpy(wf_.image.scanLine(y), wf_.image.scanLine(y - 1), (size_t)draw_w * 4u);
            }
        }

        uint32_t *row = reinterpret_cast<uint32_t *>(wf_.image.scanLine(0));
        for (int x = 0; x < draw_w; ++x) {
            int bin = (int)((long long)x * spec_snap_.bins / draw_w);
            if (bin < 0) bin = 0;
            if (bin >= spec_snap_.bins) bin = spec_snap_.bins - 1;
            uint8_t r = 0, g = 0, b = 0;
            rel_db_to_rgb(spec_snap_.rel_db[bin], &r, &g, &b);
            row[x] = qRgb(r, g, b);
        }
    }

    void draw_waterfall_panel(QPainter &p, int win_width, int win_height)
    {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_lq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
        if (panel_w < 180 || panel_h < 120) return;

        QColor color_bg("#081423");
        QColor color_grid("#4f6986");
        QColor color_border("#c4d2e4");
        QColor color_title("#8fc7ff");

        QRect panel(panel_x, panel_y, panel_w, panel_h);
        QLinearGradient panel_grad(panel.topLeft(), panel.bottomLeft());
        panel_grad.setColorAt(0.0, QColor("#101f33"));
        panel_grad.setColorAt(1.0, QColor("#081423"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(panel_grad);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setPen(QPen(color_grid, 1));
        for (int i = 1; i < 4; ++i) {
            int yy = panel_y + (panel_h * i) / 4;
            p.drawLine(panel_x + 1, yy, panel_x + panel_w - 2, yy);
        }
        for (int i = 1; i < 6; ++i) {
            int xx = panel_x + (panel_w * i) / 6;
            p.drawLine(xx, panel_y + 1, xx, panel_y + panel_h - 2);
        }

        QRect draw_rect(panel_x + kMonitorInnerPadX, panel_y + kMonitorInnerPadTop, panel_w - 2 * kMonitorInnerPadX,
                        panel_h - (kMonitorInnerPadTop + kMonitorInnerPadBottom));
        if (draw_rect.width() < 8 || draw_rect.height() < 8) return;

        p.fillRect(draw_rect, color_bg);
        if (!wf_.image.isNull()) {
            p.drawImage(draw_rect, wf_.image);
        }

        p.setPen(QPen(color_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.drawRect(draw_rect.adjusted(0, 0, -1, -1));
        p.setPen(color_title);
        p.drawText(panel_x + 8, panel_y + 16, "Signal Waterfall");

        GuiControlState st;
        {
            std::lock_guard<std::mutex> lk(g_ctrl_mtx);
            st = g_ctrl;
        }
        const double cf_mhz = mode_tx_center_hz(st.signal_mode) / 1e6;
        const double fs_mhz = (st.fs_mhz > 0.0) ? st.fs_mhz : (mode_default_fs_hz(st.signal_mode) / 1e6);
        const double half_bw_mhz = fs_mhz * 0.5;
        double f_l = cf_mhz - half_bw_mhz;
        double f_c = cf_mhz;
        double f_r = cf_mhz + half_bw_mhz;
        char lbl_l[24], lbl_c[24], lbl_r[24];
        std::snprintf(lbl_l, sizeof(lbl_l), "%.3f", f_l);
        std::snprintf(lbl_c, sizeof(lbl_c), "%.3f", f_c);
        std::snprintf(lbl_r, sizeof(lbl_r), "%.3f", f_r);
        int y_lbl = panel_y + panel_h - 4;
        p.setPen(color_border);
        p.drawText(panel_x + 6, y_lbl, lbl_l);
        p.drawText(panel_x + panel_w / 2 - 18, y_lbl, lbl_c);
        p.drawText(panel_x + panel_w - 74, y_lbl, lbl_r);
    }

    void draw_time_panel(QPainter &p, int win_width, int win_height)
    {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
        if (panel_w < 180 || panel_h < 120) return;

        QColor color_grid("#4f6986");
        QColor color_border("#c4d2e4");
        QColor color_i("#ef4444");
        QColor color_q("#51a7ff");
        QColor color_title("#8fc7ff");

        QRect panel(panel_x, panel_y, panel_w, panel_h);
        QLinearGradient panel_grad(panel.topLeft(), panel.bottomLeft());
        panel_grad.setColorAt(0.0, QColor("#101f33"));
        panel_grad.setColorAt(1.0, QColor("#081423"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(panel_grad);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setPen(QPen(color_grid, 1));
        for (int i = 1; i < 4; ++i) {
            int yy = panel_y + (panel_h * i) / 4;
            p.drawLine(panel_x + 1, yy, panel_x + panel_w - 2, yy);
        }
        for (int i = 1; i < 6; ++i) {
            int xx = panel_x + (panel_w * i) / 6;
            p.drawLine(xx, panel_y + 1, xx, panel_y + panel_h - 2);
        }

        int x0 = panel_x + 6;
        int y0 = panel_y + panel_h - 8;
        int draw_w = panel_w - 12;
        int draw_h = panel_h - 20;

        if (spec_snap_.time_valid && spec_snap_.time_samples >= 8 && draw_w > 2 && draw_h > 2) {
            float y_scale = (float)(draw_h - 1) * 0.48f;
            float y_mid = (float)y0 - (float)(draw_h - 1) * 0.5f;

            QPainterPath path_q;
            QPainterPath path_i;
            for (int i = 0; i < spec_snap_.time_samples; ++i) {
                float t = (float)i / (float)(spec_snap_.time_samples - 1);
                int x = x0 + (int)llround(t * (float)(draw_w - 1));
                int yq = (int)llround(y_mid - spec_snap_.time_q[i] * y_scale);
                int yi = (int)llround(y_mid - spec_snap_.time_i[i] * y_scale);
                if (i == 0) {
                    path_q.moveTo(x, yq);
                    path_i.moveTo(x, yi);
                } else {
                    path_q.lineTo(x, yq);
                    path_i.lineTo(x, yi);
                }
            }
            p.setPen(QPen(color_q, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path_q);
            p.setPen(QPen(color_i, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path_i);

            p.setPen(QPen(QColor(81, 167, 255, 92), 2));
            p.drawPath(path_q);
            p.setPen(QPen(QColor(239, 68, 68, 92), 2));
            p.drawPath(path_i);
        }

        p.setPen(color_border);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setPen(color_title);
        p.drawText(panel_x + 8, panel_y + 16, "Signal Time-Domain");

        p.setPen(color_border);
        p.drawText(panel_x + 8, panel_y + 48, "+1.0");
        p.drawText(panel_x + 8, panel_y + panel_h / 2, " 0.0");
        p.drawText(panel_x + 8, panel_y + panel_h - 18, "-1.0");

        double time_span_ms = ((double)spec_snap_.time_samples / FS_OUTPUT_HZ) * 1000.0;
        char t_l[24], t_c[24], t_r[24];
        std::snprintf(t_l, sizeof(t_l), "0.00ms");
        std::snprintf(t_c, sizeof(t_c), "%.2fms", time_span_ms * 0.5);
        std::snprintf(t_r, sizeof(t_r), "%.2fms", time_span_ms);
        int y_lbl = panel_y + panel_h - 4;
        p.drawText(panel_x + 6, y_lbl, t_l);
        p.drawText(panel_x + panel_w / 2 - 20, y_lbl, t_c);
        p.drawText(panel_x + panel_w - 66, y_lbl, t_r);
    }

    void draw_constellation_panel(QPainter &p, int win_width, int win_height)
    {
        int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
        get_rb_rq_panel_rect(win_width, win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
        if (panel_w < 180 || panel_h < 120) return;

        QColor color_grid("#4f6986");
        QColor color_border("#c4d2e4");
        QColor color_dot("#51a7ff");
        QColor color_title("#8fc7ff");

        QRect panel(panel_x, panel_y, panel_w, panel_h);
        QLinearGradient panel_grad(panel.topLeft(), panel.bottomLeft());
        panel_grad.setColorAt(0.0, QColor("#101f33"));
        panel_grad.setColorAt(1.0, QColor("#081423"));
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(color_border, 1));
        p.setBrush(panel_grad);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.setRenderHint(QPainter::Antialiasing, false);

        int draw_x = panel_x + kMonitorInnerPadX + 10;
        int draw_y = panel_y + kMonitorInnerPadTop;
        int draw_w = panel_w - 2 * (kMonitorInnerPadX + 10);
        int draw_h = panel_h - (kMonitorInnerPadTop + kMonitorInnerPadBottom);
        if (draw_w < 16 || draw_h < 16) return;

        p.setPen(QPen(color_grid, 1));
        // Keep panel grid but remove background grid inside the constellation inner frame.
        p.save();
        QRegion outer(panel);
        QRegion inner(draw_x, draw_y, draw_w, draw_h);
        p.setClipRegion(outer.subtracted(inner));
        for (int i = 1; i < 4; ++i) {
            int yy = panel_y + (panel_h * i) / 4;
            p.drawLine(panel_x + 1, yy, panel_x + panel_w - 2, yy);
        }
        for (int i = 1; i < 6; ++i) {
            int xx = panel_x + (panel_w * i) / 6;
            p.drawLine(xx, panel_y + 1, xx, panel_y + panel_h - 2);
        }
        p.restore();

        int cx = draw_x + draw_w / 2;
        int cy = draw_y + draw_h / 2;
        int y_shift = (panel_y + panel_h / 2) - cy;
        int cy_shift = cy + y_shift;
        int v_y0 = draw_y + y_shift;
        int v_y1 = draw_y + draw_h - 1 + y_shift;
        if (v_y0 < draw_y) v_y0 = draw_y;
        if (v_y1 > draw_y + draw_h - 1) v_y1 = draw_y + draw_h - 1;
        p.setPen(QPen(color_border, 1));
        p.drawLine(draw_x, cy_shift, draw_x + draw_w - 1, cy_shift);
        p.drawLine(cx, v_y0, cx, v_y1);

        if (spec_snap_.time_valid && spec_snap_.time_samples >= 8) {
            p.setPen(Qt::NoPen);
            p.setBrush(color_dot);
            int step = spec_snap_.time_samples / 256;
            if (step < 1) step = 1;
            for (int i = 0; i < spec_snap_.time_samples; i += step) {
                float ii = std::max(-1.0f, std::min(1.0f, spec_snap_.time_i[i]));
                float qq = std::max(-1.0f, std::min(1.0f, spec_snap_.time_q[i]));
                int px = cx + (int)llround(ii * (float)(draw_w / 2 - 2));
                int py = cy_shift - (int)llround(qq * (float)(draw_h / 2 - 2));
                p.drawEllipse(QPoint(px, py), 2, 2);
            }
        }

        p.setPen(QPen(color_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(panel.adjusted(0, 0, -1, -1), 8, 8);
        p.drawRect(draw_x, draw_y, draw_w - 1, draw_h - 1);
        p.setPen(color_title);
        p.drawText(panel_x + 8, panel_y + 16, "Signal Constellation");

        p.setPen(color_border);
        p.drawText(draw_x + 2, panel_y + panel_h - 6, "-1");
        p.drawText(draw_x + draw_w - 18, panel_y + panel_h - 6, "+1");
        p.drawText(panel_x + 2, draw_y + 10, "+1");
        p.drawText(panel_x + 6, draw_y + draw_h - 2, "-1");
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
    QNetworkAccessManager *tile_net_ = nullptr;
    QNetworkAccessManager *route_net_ = nullptr;
    std::unordered_map<QString, QPixmap> tile_cache_;
    std::set<QString> tile_pending_;
    std::unordered_map<QString, std::vector<LonLat>> route_prefetch_cache_;
    std::set<QString> route_prefetch_pending_;
    std::vector<QString> route_prefetch_order_;
    QLineEdit *inline_editor_ = nullptr;
    int inline_edit_field_ = -1;
    QRect osm_panel_rect_;
    int osm_zoom_ = 12;
    double osm_center_px_x_ = osm_lon_to_world_x(121.5654, 12);
    double osm_center_px_y_ = osm_lat_to_world_y(25.0330, 12);
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
};

static void gui_thread_main()
{
    int argc = 1;
    char app_name[] = "bds-sim";
    char *argv[] = {app_name, nullptr};

    QApplication app(argc, argv);
    g_app = &app;

    MapWidget w;
    w.showFullScreen();

    app.exec();

    g_app = nullptr;
    g_running.store(false);
}

} // namespace

extern "C" void start_map_gui(double start_bdt)
{
    if (g_running.load()) return;

    {
        std::lock_guard<std::mutex> lk(g_time_mtx);
        g_start_bdt = start_bdt;
        g_start_tp = std::chrono::steady_clock::now();
    }

    g_gui_start_req.store(0);
    g_gui_stop_req.store(0);
    g_gui_exit_req.store(0);
    map_gui_set_run_state(0);
    g_running.store(true);
    g_gui_thread = std::thread(gui_thread_main);
}

extern "C" void update_map_gui_start(double start_bdt)
{
    if (!g_running.load()) return;
    std::lock_guard<std::mutex> lk(g_time_mtx);
    g_start_bdt = start_bdt;
    g_start_tp = std::chrono::steady_clock::now();
}

extern "C" void stop_map_gui(void)
{
    if (!g_running.load() && !g_gui_thread.joinable()) return;

    g_running.store(false);
    QApplication *app = g_app;
    if (app) {
        QMetaObject::invokeMethod(app, "quit", Qt::QueuedConnection);
    }

    if (g_gui_thread.joinable()) {
        g_gui_thread.join();
    }
}

extern "C" void map_gui_set_control_defaults(const sim_config_t *cfg)
{
    if (!cfg) return;
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.tx_gain = clamp_double(cfg->tx_gain, 0.0, 100.0);
    g_ctrl.gain = clamp_double(cfg->gain, 0.1, 20.0);
    g_ctrl.signal_mode = (cfg->signal_mode <= SIG_MODE_MIXED) ? cfg->signal_mode :
                         (cfg->signal_gps ? SIG_MODE_GPS : SIG_MODE_BDS);
    g_ctrl.fs_mhz = snap_fs_to_mode_grid_hz(cfg->fs, g_ctrl.signal_mode) / 1e6;
    g_ctrl.target_cn0 = clamp_double(g_target_cn0, 20.0, 60.0);
    g_ctrl.max_ch = clamp_int(cfg->max_ch, 1, 16);
    g_ctrl.path_vmax_kmh = 72.0;
    g_ctrl.path_accel_mps2 = 2.0;
    g_ctrl.seed = cfg->seed;
    g_ctrl.sat_mode = (cfg->single_prn > 0) ? 0 : (cfg->prn37_only ? 1 : 2);
    g_ctrl.single_prn = cfg->single_prn;
    g_ctrl.single_candidate_count = 0;
    g_ctrl.single_candidate_idx = 0;
    g_ctrl.meo_only = cfg->meo_only;
    g_ctrl.byte_output = cfg->byte_output;
    g_ctrl.iono_on = cfg->iono_on;
    g_ctrl.usrp_external_clk = cfg->usrp_external_clk;
    g_ctrl.interference_mode = cfg->interference_mode;
    if (g_ctrl.single_prn < 0) g_ctrl.single_prn = 0;
    if (g_ctrl.single_prn > 63) g_ctrl.single_prn = 63;
    g_ctrl.running_ui = false;
    g_ctrl.llh_ready = false;
    if (cfg->rinex_file[0]) {
        const char *base = std::strrchr(cfg->rinex_file, '/');
        base = base ? (base + 1) : cfg->rinex_file;
        std::snprintf(g_ctrl.rinex_name, sizeof(g_ctrl.rinex_name), "%s", base);
    } else {
        std::snprintf(g_ctrl.rinex_name, sizeof(g_ctrl.rinex_name), "N/A");
    }
}

extern "C" void map_gui_set_rinex_name(const char *rinex_path)
{
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    if (!rinex_path || rinex_path[0] == '\0') {
        std::snprintf(g_ctrl.rinex_name, sizeof(g_ctrl.rinex_name), "N/A");
        return;
    }
    const char *base = std::strrchr(rinex_path, '/');
    base = base ? (base + 1) : rinex_path;
    std::snprintf(g_ctrl.rinex_name, sizeof(g_ctrl.rinex_name), "%s", base);
}

extern "C" void map_gui_get_control_config(sim_config_t *cfg, double *target_cn0)
{
    if (!cfg) return;

    std::lock_guard<std::mutex> lk(g_ctrl_mtx);

    cfg->tx_gain = clamp_double(g_ctrl.tx_gain, 0.0, 100.0);
    cfg->gain = clamp_double(g_ctrl.gain, 0.1, 20.0);
    cfg->signal_mode = g_ctrl.signal_mode;
    cfg->signal_gps = (g_ctrl.signal_mode == SIG_MODE_GPS);
    cfg->fs = snap_fs_to_mode_grid_hz(g_ctrl.fs_mhz * 1e6, g_ctrl.signal_mode);
    cfg->seed = g_ctrl.seed;

    if (g_ctrl.sat_mode == 0) {
        cfg->single_prn = g_ctrl.single_prn > 0 ? g_ctrl.single_prn : 0;
        cfg->prn37_only = false;
    } else if (g_ctrl.sat_mode == 1) {
        cfg->single_prn = 0;
        cfg->prn37_only = true;
    } else {
        cfg->single_prn = 0;
        cfg->prn37_only = false;
    }

    cfg->meo_only = g_ctrl.meo_only;
    cfg->byte_output = g_ctrl.byte_output;
    cfg->iono_on = g_ctrl.iono_on;
    cfg->usrp_external_clk = g_ctrl.usrp_external_clk;
    cfg->interference_mode = g_ctrl.interference_mode;
    cfg->max_ch = clamp_int(g_ctrl.max_ch, 1, 16);

    if (target_cn0) {
        *target_cn0 = clamp_double(g_ctrl.target_cn0, 20.0, 60.0);
    }
}

extern "C" void map_gui_set_single_prn_candidates(const int *prns, int count)
{
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);

    int n = count;
    if (n < 0) n = 0;
    if (n > 64) n = 64;
    g_ctrl.single_candidate_count = n;

    for (int i = 0; i < n; ++i) {
        int v = prns ? prns[i] : 0;
        g_ctrl.single_candidates[i] = clamp_int(v, 1, 63);
    }

    if (n <= 0) {
        g_ctrl.single_prn = 0;
        g_ctrl.single_candidate_idx = 0;
        return;
    }

    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (g_ctrl.single_candidates[i] == g_ctrl.single_prn) {
            idx = i;
            break;
        }
    }
    if (idx < 0) idx = 0;
    g_ctrl.single_candidate_idx = idx;
    g_ctrl.single_prn = g_ctrl.single_candidates[g_ctrl.single_candidate_idx];
}

extern "C" void map_gui_set_run_state(int running)
{
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.running_ui = running ? true : false;
}

extern "C" void map_gui_set_llh_ready(int ready)
{
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    g_ctrl.llh_ready = ready ? true : false;
}

extern "C" int map_gui_get_preview_prn(void)
{
    std::lock_guard<std::mutex> lk(g_ctrl_mtx);
    if (g_ctrl.single_prn < 1 || g_ctrl.single_prn > 63) return 0;
    return g_ctrl.single_prn;
}

extern "C" int map_gui_consume_selected_llh(double *lat_deg, double *lon_deg, double *h_m)
{
    uint32_t n = g_gui_llh_pick_req.exchange(0);
    if (n == 0) return 0;

    std::lock_guard<std::mutex> lk(g_llh_pick_mtx);
    if (lat_deg) *lat_deg = g_llh_pick_lat_deg;
    if (lon_deg) *lon_deg = g_llh_pick_lon_deg;
    if (h_m) *h_m = g_llh_pick_h_m;
    return 1;
}

extern "C" int map_gui_consume_start_request(void)
{
    uint32_t n = g_gui_start_req.exchange(0);
    return n > 0 ? 1 : 0;
}

extern "C" int map_gui_consume_stop_request(void)
{
    uint32_t n = g_gui_stop_req.exchange(0);
    return n > 0 ? 1 : 0;
}

extern "C" int map_gui_consume_exit_request(void)
{
    uint32_t n = g_gui_exit_req.exchange(0);
    return n > 0 ? 1 : 0;
}

extern "C" void map_gui_notify_path_segment_started(void)
{
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    if (!g_path_segments.empty()) {
        g_path_segments.front().state = PATH_SEG_EXECUTING;
    }
}

extern "C" void map_gui_notify_path_segment_finished(void)
{
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    if (!g_path_segments.empty()) {
        g_path_segments.erase(g_path_segments.begin());
    }
}

extern "C" void map_gui_notify_path_segment_undo(void)
{
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    if (!g_path_segments.empty() && g_path_segments.back().state == PATH_SEG_QUEUED) {
        g_path_segments.pop_back();
    }
}

extern "C" void map_gui_clear_path_segments(void)
{
    std::lock_guard<std::mutex> lk(g_path_seg_mtx);
    g_path_segments.clear();
}

extern "C" void map_gui_pump_events(void)
{
    QApplication *app = g_app;
    if (app) app->processEvents();
}
