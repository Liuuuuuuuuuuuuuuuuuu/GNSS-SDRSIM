#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QSet>
#include <QSocketNotifier>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

int clamp_int(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

double clamp_double(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}

QColor lerp_color(const QColor &a, const QColor &b, double t) {
    t = clamp_double(t, 0.0, 1.0);
    const int r = static_cast<int>(a.red() + (b.red() - a.red()) * t);
    const int g = static_cast<int>(a.green() + (b.green() - a.green()) * t);
    const int bl = static_cast<int>(a.blue() + (b.blue() - a.blue()) * t);
    return QColor(r, g, bl);
}

struct Star {
    double x = 0.0;
    double y = 0.0;
    int r = 1;
    double phase = 0.0;
    double speed = 1.0;
};

struct SatAnim {
    double t0 = 0.0;
    double speed = 0.55;
    double spin_phase = 0.0;
    double spin_speed = 1.0;
};

struct RinexSat {
    QChar sys;
    QString prn;
    double M0 = 0.0;
    double e = 0.0;
    double sqrt_a = 0.0;
    double toe = 0.0;
    double omega0 = 0.0;
    double i0 = 0.0;
    double omega = 0.0;
};

double parse_field(const QString &line, int i) {
    const int start = 4 + i * 19;
    if (start < 0 || start >= line.size()) return 0.0;
    bool ok = false;
    const double v = line.mid(start, 19).trimmed().toDouble(&ok);
    return ok ? v : 0.0;
}

bool fetch_ip_latlon(double *lat_deg, double *lon_deg) {
    QTcpSocket sock;
    sock.connectToHost("ip-api.com", 80);
    if (!sock.waitForConnected(1500)) {
        return false;
    }

    const QByteArray req(
        "GET /json/?fields=lat,lon HTTP/1.1\r\n"
        "Host: ip-api.com\r\n"
        "Connection: close\r\n\r\n");
    sock.write(req);
    if (!sock.waitForBytesWritten(1000)) {
        return false;
    }

    QByteArray resp;
    while (sock.state() != QAbstractSocket::UnconnectedState) {
        if (!sock.waitForReadyRead(1200)) {
            break;
        }
        resp += sock.readAll();
    }
    resp += sock.readAll();

    const int split = resp.indexOf("\r\n\r\n");
    if (split < 0) return false;
    const QByteArray body = resp.mid(split + 4);
    const QJsonDocument jd = QJsonDocument::fromJson(body);
    if (!jd.isObject()) return false;
    const QJsonObject obj = jd.object();
    if (!obj.contains("lat") || !obj.contains("lon")) return false;
    *lat_deg = obj.value("lat").toDouble(25.04);
    *lon_deg = obj.value("lon").toDouble(121.51);
    return true;
}

} // namespace

class SplashWindow final : public QWidget {
public:
    explicit SplashWindow(const QString &logo_path)
        : QWidget(nullptr), stdin_notifier_(new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this)) {
        setWindowTitle("GNSS");
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
        setMinimumSize(520, 320);

        const QRect scr = QApplication::primaryScreen()->availableGeometry();
        const int width = clamp_int(static_cast<int>(scr.width() * 0.46), 520, 960);
        const int height = clamp_int(static_cast<int>(scr.height() * 0.44), 320, 680);
        const int x = std::max(0, (scr.width() - width) / 2 + scr.x());
        const int y = std::max(0, (scr.height() - height) / 2 + scr.y());
        windowed_geometry_ = QRect(x, y, width, height);
        setGeometry(windowed_geometry_);

        is_fullscreen_ = true;
        showFullScreen();

        setWindowOpacity(0.0);
        setAutoFillBackground(false);

        logo_src_ = QPixmap(logo_path);

        init_sky_scene();

        QObject::connect(stdin_notifier_, &QSocketNotifier::activated, this, [this]() { on_stdin_ready(); });

        anim_timer_.setInterval(33);
        QObject::connect(&anim_timer_, &QTimer::timeout, this, [this]() { on_frame(); });
        anim_timer_.start();

        started_ms_ = QDateTime::currentMSecsSinceEpoch();

        counts_thread_ = std::thread([this]() { fetch_counts(); });
    }

    ~SplashWindow() override {
        running_.store(false);
        if (counts_thread_.joinable()) {
            counts_thread_.join();
        }
    }

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_F11) {
            is_fullscreen_ = !is_fullscreen_;
            if (is_fullscreen_) {
                showFullScreen();
            } else {
                showNormal();
                setGeometry(windowed_geometry_);
            }
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            close();
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }

    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        layout_cache_valid_ = false;
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const double elapsed = (QDateTime::currentMSecsSinceEpoch() - started_ms_) / 1000.0;
        draw_background(p, elapsed);
        update_layout();
        draw_sky(p, elapsed);
        draw_logo(p, elapsed);
        draw_progress(p, elapsed);
        draw_texts(p, elapsed);
    }

private:
    void init_sky_scene() {
        std::mt19937 rng(20260429);
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        std::uniform_int_distribution<int> rdist(0, 3);

        stars_.clear();
        stars_.reserve(90);
        for (int i = 0; i < 90; ++i) {
            Star s;
            s.x = uni(rng);
            s.y = uni(rng);
            const int pick = rdist(rng);
            s.r = (pick == 3) ? 2 : 1;
            s.phase = uni(rng) * 6.283185307179586;
            s.speed = 0.7 + uni(rng) * 1.8;
            stars_.push_back(s);
        }

        make_sat_items(8, 12, 20260430);
    }

    void make_sat_items(int n_gps, int n_bds, int seed) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> uni(0.0, 1.0);

        std::vector<SatAnim> gps;
        std::vector<SatAnim> bds;
        gps.reserve(n_gps);
        bds.reserve(n_bds);

        const double gps_phase = uni(rng);
        const double bds_phase = uni(rng);

        for (int i = 0; i < n_gps; ++i) {
            SatAnim s;
            s.t0 = std::fmod((i + gps_phase) / std::max(1, n_gps), 1.0);
            s.speed = 0.55 + uni(rng) * 0.30;
            s.spin_phase = uni(rng) * 6.283185307179586;
            s.spin_speed = 0.9 + uni(rng) * 1.7;
            gps.push_back(s);
        }
        for (int i = 0; i < n_bds; ++i) {
            SatAnim s;
            s.t0 = std::fmod((i + bds_phase) / std::max(1, n_bds), 1.0);
            s.speed = 0.50 + uni(rng) * 0.30;
            s.spin_phase = uni(rng) * 6.283185307179586;
            s.spin_speed = 0.9 + uni(rng) * 1.7;
            bds.push_back(s);
        }

        {
            std::lock_guard<std::mutex> lk(sat_mutex_);
            gps_sats_.swap(gps);
            bds_sats_.swap(bds);
            n_gps_ = n_gps;
            n_bds_ = n_bds;
        }
    }

    void update_layout() {
        if (layout_cache_valid_ && cached_size_ == size()) {
            return;
        }
        cached_size_ = size();
        layout_cache_valid_ = true;

        const int w = std::max(1, width());
        const int h = std::max(1, height());

        title_rect_ = QRectF(0, h * 0.06, w, h * 0.10);
        progress_center_y_ = h * 0.77;
        orbit_bottom_limit_ = progress_center_y_ - std::max(34.0, h * 0.14);

        const double logo_nominal_y = h * 0.50;
        const double logo_min_y = h * 0.34;
        const double logo_max_y = std::max(logo_min_y, orbit_bottom_limit_ - std::max(72.0, h * 0.22));
        logo_base_ = QPointF(w * 0.5, std::max(logo_min_y, std::min(logo_nominal_y, logo_max_y)));

        int target_w = 0;
        if (!logo_src_.isNull()) {
            const int src_w = std::max(1, logo_src_.width());
            const int src_h = std::max(1, logo_src_.height());
            const double aspect = static_cast<double>(src_w) / src_h;
            const int max_w = static_cast<int>(w * 0.56);
            const int max_h = static_cast<int>(h * 0.42);
            target_w = static_cast<int>(std::min<double>(max_w, max_h * aspect));
            target_w = clamp_int(target_w, 170, 760);
            if (target_w != logo_target_w_) {
                logo_target_w_ = target_w;
                if (target_w >= src_w) {
                    logo_scaled_ = logo_src_;
                } else {
                    logo_scaled_ = logo_src_.scaledToWidth(target_w, Qt::SmoothTransformation);
                }
            }
        }

        int lw = std::max(160, static_cast<int>(w * 0.22));
        int lh = std::max(80, static_cast<int>(h * 0.11));
        if (!logo_scaled_.isNull()) {
            lw = logo_scaled_.width();
            lh = logo_scaled_.height();
        }
        if (lw <= 2 || lh <= 2) {
            lw = std::max(180, static_cast<int>(w * 0.34));
            lh = std::max(140, static_cast<int>(h * 0.30));
            orbit_center_ = QPointF(w * 0.5, h * 0.50);
        } else {
            orbit_center_ = logo_base_;
        }

        const double view_scale = std::min(w, h);
        const double rx_logo = lw * 0.70 + 38.0;
        const double ry_logo = lh * 0.54 + 20.0;
        const double rx_view = view_scale * 0.235;
        const double ry_view = view_scale * 0.155;
        const double max_rx = std::max(90.0, std::min(orbit_center_.x() - 28.0, w - orbit_center_.x() - 28.0));
        const double max_ry = std::max(48.0, std::min(orbit_center_.y() - 28.0, orbit_bottom_limit_ - orbit_center_.y()));
        orbit_rx_ = std::min(max_rx, std::max(rx_logo, rx_view));
        orbit_ry_ = std::min(max_ry, std::max(ry_logo, ry_view));

        const double bar_relw = std::min(0.86, std::max(0.55, 420.0 / std::max(1.0, static_cast<double>(w))));
        const double pb_w = w * bar_relw;
        const double pb_h = std::max(16.0, std::min(24.0, h * 0.028));
        progress_rect_ = QRectF(w * 0.5 - pb_w * 0.5, progress_center_y_ - pb_h * 0.5, pb_w, pb_h);
        status_rect_ = QRectF(w * 0.05, h * 0.82, w * 0.90, h * 0.12);
        percent_rect_ = QRectF(0, h * 0.91, w, h * 0.07);
    }

    void draw_background(QPainter &p, double elapsed) {
        const double bg_t = 0.5 + 0.5 * std::sin(elapsed * 0.9);
        const double glow_t = 0.5 + 0.5 * std::sin(elapsed * 1.4 + 0.8);
        const QColor bg = lerp_color(QColor(15, 23, 42), QColor(19, 38, 71), bg_t);
        const QColor glow = lerp_color(QColor(20, 46, 88), QColor(14, 116, 144), glow_t);
        const QColor final_bg = lerp_color(bg, glow, 0.33);
        p.fillRect(rect(), final_bg);

        const double alpha = clamp_double(elapsed / 0.35, 0.0, 1.0);
        setWindowOpacity(alpha);
    }

    void draw_sky(QPainter &p, double elapsed) {
        const int w = std::max(1, width());
        const int h = std::max(1, height());

        p.setPen(Qt::NoPen);
        for (const Star &s : stars_) {
            const double tw = 0.5 + 0.5 * std::sin(elapsed * s.speed + s.phase);
            const int c = static_cast<int>(125 + 110 * tw);
            p.setBrush(QColor(c, std::min(255, c + 14), 255));
            const double x = s.x * w;
            const double y = s.y * h;
            p.drawEllipse(QPointF(x, y), s.r, s.r);
        }

        const QColor hud(36, 64, 95);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(hud, 2));
        const double m = 26.0;
        const double ln = 52.0;
        p.drawLine(QPointF(m, m), QPointF(m + ln, m));
        p.drawLine(QPointF(m, m), QPointF(m, m + ln));
        p.drawLine(QPointF(w - m, m), QPointF(w - m - ln, m));
        p.drawLine(QPointF(w - m, m), QPointF(w - m, m + ln));
        p.drawLine(QPointF(m, h - m), QPointF(m + ln, h - m));
        p.drawLine(QPointF(m, h - m), QPointF(m, h - m - ln));
        p.drawLine(QPointF(w - m, h - m), QPointF(w - m - ln, h - m));
        p.drawLine(QPointF(w - m, h - m), QPointF(w - m, h - m - ln));

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(53, 95, 134), 5));
        p.drawEllipse(QPointF(orbit_center_.x(), orbit_center_.y()), orbit_rx_, orbit_ry_);
        p.setPen(QPen(QColor(79, 134, 182), 3));
        p.drawEllipse(QPointF(orbit_center_.x(), orbit_center_.y()), orbit_rx_, orbit_ry_);
        p.setPen(QPen(QColor(127, 176, 218), 1));
        p.drawEllipse(QPointF(orbit_center_.x(), orbit_center_.y()), orbit_rx_, orbit_ry_);

        std::vector<SatAnim> gps;
        std::vector<SatAnim> bds;
        {
            std::lock_guard<std::mutex> lk(sat_mutex_);
            gps = gps_sats_;
            bds = bds_sats_;
        }

        draw_sat_group(p, gps, elapsed, 90.0, 180.0, true);
        draw_sat_group(p, bds, elapsed, -90.0, 180.0, false);
    }

    void draw_sat_group(QPainter &p, const std::vector<SatAnim> &sats, double elapsed,
                        double arc_start_deg, double arc_extent_deg, bool gps) {
        for (const SatAnim &s : sats) {
            const double t = std::fmod(s.t0 + elapsed * (s.speed / 8.0), 1.0);
            const double ang_deg = arc_start_deg + arc_extent_deg * t;
            const double ang = ang_deg * M_PI / 180.0;

            const double sx = orbit_center_.x() + orbit_rx_ * std::cos(ang);
            const double sy = orbit_center_.y() + orbit_ry_ * std::sin(ang);

            double depth = 0.5 + 0.5 * std::sin(ang);
            if (!gps) {
                depth = 0.5 + 0.5 * std::sin(ang + M_PI);
            }

            const int alpha = static_cast<int>(140 + 115 * depth);
            const double body_hu = (gps ? 3.5 : 3.6) + depth * (gps ? 1.4 : 1.5);
            const double body_hv = (gps ? 2.3 : 2.4) + depth * 1.0;
            const double panel_hu = (gps ? 5.2 : 5.1) + depth * 2.0;
            const double panel_hv = 1.5 + depth * 0.8;
            const double gap_t = (gps ? 6.0 : 6.0) + depth * (gps ? 2.2 : 2.1);

            const double tx = -orbit_rx_ * std::sin(ang);
            const double ty = orbit_ry_ * std::cos(ang);
            const double base_heading = std::atan2(ty, tx);
            const double heading = base_heading + 0.30 * std::sin(elapsed * s.spin_speed + s.spin_phase);
            const double ux = std::cos(heading);
            const double uy = std::sin(heading);

            double nx = sx - orbit_center_.x();
            double ny = sy - orbit_center_.y();
            const double nlen = std::sqrt(nx * nx + ny * ny);
            if (nlen > 1e-6) {
                nx /= nlen;
                ny /= nlen;
            } else {
                nx = -uy;
                ny = ux;
            }

            const double panel_lift = body_hv + panel_hv + 1.8;
            const QPointF pl(sx - gap_t * ux + panel_lift * nx, sy - gap_t * uy + panel_lift * ny);
            const QPointF pr(sx + gap_t * ux + panel_lift * nx, sy + gap_t * uy + panel_lift * ny);

            const QPolygonF body = sat_rect(sx, sy, ux, uy, nx, ny, body_hu, body_hv);
            const QPolygonF left_panel = sat_rect(pl.x(), pl.y(), ux, uy, nx, ny, panel_hu, panel_hv);
            const QPolygonF right_panel = sat_rect(pr.x(), pr.y(), ux, uy, nx, ny, panel_hu, panel_hv);

            if (gps) {
                p.setPen(QPen(QColor(148, 163, 184), 1));
                p.drawLine(QPointF(sx - 1.6 * ux, sy - 1.6 * uy), pl);
                p.drawLine(QPointF(sx + 1.6 * ux, sy + 1.6 * uy), pr);
                p.setPen(QPen(QColor(147, 197, 253), 1));
                p.setBrush(QColor(29, 78, 216));
                p.drawPolygon(left_panel);
                p.drawPolygon(right_panel);
                p.setPen(QPen(QColor(248, 250, 252), 1));
                p.setBrush(QColor(alpha, std::min(255, alpha + 18), std::min(255, alpha + 32)));
                p.drawPolygon(body);
            } else {
                const int amber_r = std::min(255, alpha + 36);
                const int amber_g = std::min(255, static_cast<int>(alpha * 0.74));
                p.setPen(QPen(QColor(168, 162, 158), 1));
                p.drawLine(QPointF(sx - 1.6 * ux, sy - 1.6 * uy), pl);
                p.drawLine(QPointF(sx + 1.6 * ux, sy + 1.6 * uy), pr);
                p.setPen(QPen(QColor(245, 158, 11), 1));
                p.setBrush(QColor(30, 58, 138));
                p.drawPolygon(left_panel);
                p.drawPolygon(right_panel);
                p.setPen(QPen(QColor(254, 243, 199), 1));
                p.setBrush(QColor(amber_r, amber_g, alpha / 4));
                p.drawPolygon(body);
            }
        }
    }

    QPolygonF sat_rect(double px, double py, double ux, double uy, double nx, double ny, double hu, double hv) {
        QPolygonF poly;
        poly << QPointF(px - hu * ux - hv * nx, py - hu * uy - hv * ny)
             << QPointF(px + hu * ux - hv * nx, py + hu * uy - hv * ny)
             << QPointF(px + hu * ux + hv * nx, py + hu * uy + hv * ny)
             << QPointF(px - hu * ux + hv * nx, py - hu * uy + hv * ny);
        return poly;
    }

    void draw_logo(QPainter &p, double elapsed) {
        Q_UNUSED(elapsed);
        if (!logo_scaled_.isNull()) {
            const QPointF tl(logo_base_.x() - logo_scaled_.width() * 0.5,
                             logo_base_.y() - logo_scaled_.height() * 0.5);
            p.drawPixmap(tl, logo_scaled_);
            return;
        }

        const double logo_t = 0.5 + 0.5 * std::sin(elapsed * 2.2);
        const QColor logo_col = lerp_color(QColor(96, 165, 250), QColor(186, 230, 253), logo_t);
        QFont f("DejaVu Sans", clamp_int(static_cast<int>(height() * 0.09), 22, 56), QFont::Bold);
        p.setFont(f);
        p.setPen(logo_col);
        const QRectF r(logo_base_.x() - 180, logo_base_.y() - 50, 360, 100);
        p.drawText(r, Qt::AlignCenter, "GNSS");
    }

    void draw_progress(QPainter &p, double elapsed) {
        const int shown_pct = clamp_int(static_cast<int>(std::round(display_pct_)), 0, 100);
        const double hue_t = 0.5 + 0.5 * std::sin(elapsed * 1.4);
        const double scan_t = 0.5 + 0.5 * std::sin(elapsed * 3.2);
        const QColor trough = lerp_color(QColor(30, 41, 59), QColor(22, 78, 99), scan_t);
        const QColor bar = lerp_color(QColor(14, 116, 144), QColor(56, 189, 248), hue_t);
        const QColor hi = lerp_color(QColor(56, 189, 248), QColor(186, 230, 253), hue_t);

        const QRectF frame = progress_rect_.adjusted(-3, -3, 3, 3);
        const QRectF glow = progress_rect_.adjusted(-7, -7, 7, 7);
        const double fx_t = 0.5 + 0.5 * std::sin(elapsed * 2.8);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(lerp_color(QColor(34, 211, 238), QColor(125, 211, 252), fx_t), 1));
        p.drawRect(frame);
        p.setPen(QPen(lerp_color(QColor(8, 47, 73), QColor(14, 116, 144), fx_t), 1));
        p.drawRect(glow);

        p.setPen(Qt::NoPen);
        p.setBrush(trough);
        p.drawRoundedRect(progress_rect_, 4, 4);

        const double fill_w = progress_rect_.width() * shown_pct / 100.0;
        const QRectF fill(progress_rect_.x(), progress_rect_.y(), fill_w, progress_rect_.height());
        p.setBrush(bar);
        p.drawRoundedRect(fill, 4, 4);

        p.setPen(QPen(hi, 1));
        p.drawLine(QPointF(progress_rect_.x(), progress_rect_.y() + 1),
                   QPointF(progress_rect_.x() + fill_w, progress_rect_.y() + 1));

        for (int i = 1; i < 24; ++i) {
            const double tx = progress_rect_.x() + progress_rect_.width() * (static_cast<double>(i) / 24.0);
            const double tt = 0.5 + 0.5 * std::sin(elapsed * 4.2 + i * 0.35);
            p.setPen(QPen(lerp_color(QColor(22, 78, 99), QColor(56, 189, 248), tt), 1));
            p.drawLine(QPointF(tx, progress_rect_.y() - 2), QPointF(tx, progress_rect_.y() + progress_rect_.height() + 2));
        }

        if (fill_w > 1.0) {
            // Sweep only across completed progress so users feel the bar is being pushed forward.
            const double scan_phase = std::fmod(elapsed * 1.15, 1.0);
            const double scan_x = progress_rect_.x() + fill_w * scan_phase;
            const double trail_alpha = 0.26 + 0.20 * (0.5 + 0.5 * std::sin(elapsed * 3.6));
            const QColor trail_color(110, 231, 255, static_cast<int>(255.0 * trail_alpha));
            const QRectF trail(progress_rect_.x(), progress_rect_.y() + 1.0,
                               std::max(1.0, scan_x - progress_rect_.x()),
                               std::max(2.0, progress_rect_.height() - 2.0));
            p.setPen(Qt::NoPen);
            p.setBrush(trail_color);
            p.drawRoundedRect(trail, 3, 3);

            p.setPen(QPen(QColor(165, 243, 252), 2));
            p.drawLine(QPointF(scan_x, frame.y() + 1), QPointF(scan_x, frame.y() + frame.height() - 1));
        }
    }

    void draw_texts(QPainter &p, double elapsed) {
        const int h = std::max(1, height());
        const int title_fs = clamp_int(static_cast<int>(h * 0.042), 14, 28);
        const int info_fs = clamp_int(static_cast<int>(h * 0.028), 10, 16);
        const int pct_fs = clamp_int(static_cast<int>(h * 0.03), 10, 18);

        const double pulse = 0.5 + 0.5 * std::sin(elapsed * 2.4);
        const int c = static_cast<int>(200 + 55 * pulse);
        p.setPen(QColor(c, c, std::min(255, c + 12)));
        p.setFont(QFont("DejaVu Sans", title_fs, QFont::Bold));
        p.drawText(title_rect_, Qt::AlignCenter, "GNSS");

        QString status = base_status_;
        if (!error_mode_ && status.contains("Compiling")) {
            status += QString(".").repeated((static_cast<int>(elapsed * 2.6)) % 4);
        }
        p.setPen(error_mode_ ? QColor(254, 202, 202) : QColor(203, 213, 225));
        p.setFont(QFont("DejaVu Sans", info_fs));
        p.drawText(status_rect_, Qt::AlignCenter | Qt::TextWordWrap, status);

        p.setPen(error_mode_ ? QColor(254, 202, 202) : QColor(125, 211, 252));
        p.setFont(QFont("DejaVu Sans", pct_fs, QFont::Bold));
        if (error_mode_) {
            p.drawText(percent_rect_, Qt::AlignCenter, "ERROR");
        } else {
            const int shown_pct = clamp_int(static_cast<int>(std::round(display_pct_)), 0, 100);
            p.drawText(percent_rect_, Qt::AlignCenter, QString::number(shown_pct) + "%");
        }
    }

    void on_stdin_ready() {
        char buf[2048];
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) {
            stdin_notifier_->setEnabled(false);
            if (close_after_ms_ < 0) {
                close_after_ms_ = 800;
            }
            return;
        }

        pending_.append(buf, static_cast<int>(n));
        int idx = pending_.indexOf('\n');
        while (idx >= 0) {
            const QByteArray line = pending_.left(idx).trimmed();
            pending_.remove(0, idx + 1);
            if (!line.isEmpty()) {
                handle_line(QString::fromUtf8(line));
            }
            idx = pending_.indexOf('\n');
        }
    }

    void handle_line(const QString &line) {
        const QStringList parts = line.split('\t');
        if (parts.isEmpty()) return;

        const QString cmd = parts[0].trimmed().toUpper();
        if (cmd == "PROGRESS") {
            int pct = 0;
            if (parts.size() >= 2) {
                bool ok = false;
                pct = parts[1].toInt(&ok);
                if (!ok) pct = 0;
            }
            target_pct_ = static_cast<double>(clamp_int(pct, 0, 100));
            if (parts.size() >= 3) {
                base_status_ = parts.mid(2).join("\t");
            }
            return;
        }

        if (cmd == "ERROR") {
            error_mode_ = true;
            target_pct_ = 100.0;
            base_status_ = QString("Build failed: %1").arg(parts.size() >= 2 ? parts.mid(1).join("\t") : QString("unknown error"));
            return;
        }

        if (cmd == "CLOSE") {
            int delay = 600;
            if (parts.size() >= 2) {
                bool ok = false;
                const int parsed = parts[1].toInt(&ok);
                if (ok) delay = parsed;
            }
            close_after_ms_ = clamp_int(delay, 0, 10000);
            return;
        }

        base_status_ = line;
    }

    void on_frame() {
        if (std::abs(target_pct_ - display_pct_) > 0.01) {
            const double diff = target_pct_ - display_pct_;
            const double step = std::max(0.08, std::abs(diff) * 0.16);
            if (diff > 0) display_pct_ = std::min(target_pct_, display_pct_ + step);
            else display_pct_ = std::max(target_pct_, display_pct_ - step);
        }

        if (close_after_ms_ >= 0) {
            if (!close_armed_) {
                close_armed_ = true;
                QTimer::singleShot(close_after_ms_, this, [this]() { close(); });
            }
        }

        update();
    }

    QString latest_rnx_path() {
        const QString brdm = QDir::current().filePath("BRDM");
        QDir dir(brdm);
        if (!dir.exists()) {
            return QString();
        }
        QStringList files = dir.entryList(QStringList() << "BRDC00WRD_S_*_MN.rnx", QDir::Files, QDir::Name);
        std::reverse(files.begin(), files.end());
        if (files.isEmpty()) return QString();

        const int cap = std::min(48, files.size());
        for (int i = 0; i < cap; ++i) {
            const QString path = dir.filePath(files[i]);
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            bool has_g = false;
            bool has_c = false;
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString ln = ts.readLine();
                if (ln.startsWith("> EPH G")) has_g = true;
                else if (ln.startsWith("> EPH C")) has_c = true;
                if (has_g && has_c) return path;
            }
        }

        return dir.filePath(files[0]);
    }

    std::vector<RinexSat> parse_rnx_sats(const QString &path) {
        std::vector<RinexSat> out;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return out;
        }

        QTextStream ts(&f);
        bool in_header = true;
        RinexSat cur;
        bool has_cur = false;
        int line_num = 0;

        while (!ts.atEnd()) {
            const QString line = ts.readLine();
            if (in_header) {
                if (line.contains("END OF HEADER")) {
                    in_header = false;
                }
                continue;
            }

            if (line.startsWith("> EPH")) {
                const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    const QChar c = parts[2].at(0);
                    if (c == 'G' || c == 'C') {
                        cur = RinexSat();
                        cur.sys = c;
                        cur.prn = parts[2];
                        has_cur = true;
                        line_num = 0;
                        continue;
                    }
                }
                has_cur = false;
                continue;
            }

            if (!has_cur) continue;
            ++line_num;
            if (line_num == 2) {
                cur.M0 = parse_field(line, 3);
            } else if (line_num == 3) {
                cur.e = parse_field(line, 1);
                cur.sqrt_a = parse_field(line, 3);
            } else if (line_num == 4) {
                cur.toe = parse_field(line, 0);
                cur.omega0 = parse_field(line, 2);
            } else if (line_num == 5) {
                cur.i0 = parse_field(line, 0);
                cur.omega = parse_field(line, 2);
                out.push_back(cur);
                has_cur = false;
            }
        }

        return out;
    }

    static double gps_sow() {
        const QDateTime gps_epoch(QDate(1980, 1, 6), QTime(0, 0), Qt::UTC);
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 ms = gps_epoch.msecsTo(now);
        const double sec = static_cast<double>(ms) / 1000.0;
        return std::fmod(sec, 604800.0);
    }

    static double solve_kepler(double M, double e) {
        double E = M;
        for (int i = 0; i < 12; ++i) {
            const double dE = (M - E + e * std::sin(E)) / (1.0 - e * std::cos(E));
            E += dE;
            if (std::abs(dE) < 1e-12) break;
        }
        return E;
    }

    static void user_ecef(double lat_r, double lon_r, double *ux, double *uy, double *uz) {
        const double a_e = 6378137.0;
        const double e2 = 0.00669437999014;
        const double N = a_e / std::sqrt(1 - e2 * std::sin(lat_r) * std::sin(lat_r));
        *ux = N * std::cos(lat_r) * std::cos(lon_r);
        *uy = N * std::cos(lat_r) * std::sin(lon_r);
        *uz = N * (1 - e2) * std::sin(lat_r);
    }

    static double elevation(const RinexSat &s, double t,
                            double cos_lat, double sin_lat,
                            double cos_lon, double sin_lon,
                            double ux, double uy, double uz) {
        const double GM = 3.986005e14;
        const double OMEGA_E = 7.2921151467e-5;
        const double a = s.sqrt_a * s.sqrt_a;
        if (a <= 0.0) return -90.0;
        const double n = std::sqrt(GM / (a * a * a));
        double tk = t - s.toe;
        if (tk > 302400.0) tk -= 604800.0;
        else if (tk < -302400.0) tk += 604800.0;

        const double M = s.M0 + n * tk;
        const double E = solve_kepler(M, s.e);
        const double nu = std::atan2(std::sqrt(1 - s.e * s.e) * std::sin(E), std::cos(E) - s.e);
        const double u = s.omega + nu;
        const double r = a * (1 - s.e * std::cos(E));
        const double xp = r * std::cos(u);
        const double yp = r * std::sin(u);

        const double Om = s.omega0 - OMEGA_E * (s.toe + tk);
        const double cos_Om = std::cos(Om);
        const double sin_Om = std::sin(Om);
        const double cos_i = std::cos(s.i0);
        const double sin_i = std::sin(s.i0);
        const double X = xp * cos_Om - yp * cos_i * sin_Om;
        const double Y = xp * sin_Om + yp * cos_i * cos_Om;
        const double Z = yp * sin_i;

        const double dx = X - ux;
        const double dy = Y - uy;
        const double dz = Z - uz;
        const double rng2 = dx * dx + dy * dy + dz * dz;
        if (rng2 < 1e6) return -90.0;
        const double rng = std::sqrt(rng2);
        const double e_u = cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;
        const double s_el = clamp_double(e_u / rng, -1.0, 1.0);
        return std::asin(s_el) * 180.0 / M_PI;
    }

    void fetch_counts() {
        if (!running_.load()) return;

        std::vector<RinexSat> sats;
        const QString path = latest_rnx_path();
        if (!path.isEmpty()) {
            sats = parse_rnx_sats(path);
        }

        double lat_deg = 25.04;
        double lon_deg = 121.51;
        fetch_ip_latlon(&lat_deg, &lon_deg);

        const double lat_r = lat_deg * M_PI / 180.0;
        const double lon_r = lon_deg * M_PI / 180.0;
        const double cos_lat = std::cos(lat_r);
        const double sin_lat = std::sin(lat_r);
        const double cos_lon = std::cos(lon_r);
        const double sin_lon = std::sin(lon_r);

        double ux = 0.0;
        double uy = 0.0;
        double uz = 0.0;
        user_ecef(lat_r, lon_r, &ux, &uy, &uz);

        const double t = gps_sow();
        int n_gps = 0;
        int n_bds = 0;
        QSet<QString> seen;
        for (const RinexSat &s : sats) {
            if (seen.contains(s.prn)) continue;
            const double el = elevation(s, t, cos_lat, sin_lat, cos_lon, sin_lon, ux, uy, uz);
            if (el > 5.0) {
                seen.insert(s.prn);
                if (s.sys == 'G') ++n_gps;
                else if (s.sys == 'C') ++n_bds;
            }
        }

        n_gps = (n_gps > 0) ? clamp_int(n_gps, 4, 14) : 8;
        n_bds = (n_bds > 0) ? clamp_int(n_bds, 4, 16) : 12;

        QMetaObject::invokeMethod(this, [this, n_gps, n_bds]() {
            make_sat_items(n_gps, n_bds, static_cast<int>(QDateTime::currentMSecsSinceEpoch() & 0x7fffffff));
        }, Qt::QueuedConnection);
    }

private:
    QSocketNotifier *stdin_notifier_ = nullptr;
    QByteArray pending_;

    bool is_fullscreen_ = true;
    QRect windowed_geometry_;

    QPixmap logo_src_;
    QPixmap logo_scaled_;
    int logo_target_w_ = 0;

    bool layout_cache_valid_ = false;
    QSize cached_size_;
    QPointF logo_base_;
    QPointF orbit_center_;
    double orbit_rx_ = 120.0;
    double orbit_ry_ = 80.0;
    double progress_center_y_ = 0.0;
    double orbit_bottom_limit_ = 0.0;
    QRectF title_rect_;
    QRectF status_rect_;
    QRectF percent_rect_;
    QRectF progress_rect_;

    std::vector<Star> stars_;
    std::vector<SatAnim> gps_sats_;
    std::vector<SatAnim> bds_sats_;
    std::mutex sat_mutex_;
    int n_gps_ = 8;
    int n_bds_ = 12;

    QTimer anim_timer_;
    qint64 started_ms_ = 0;

    double target_pct_ = 0.0;
    double display_pct_ = 0.0;
    QString base_status_ = "Preparing...";
    bool error_mode_ = false;

    int close_after_ms_ = -1;
    bool close_armed_ = false;

    std::atomic<bool> running_{true};
    std::thread counts_thread_;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QString logo_path;
    if (argc >= 2) {
        logo_path = QString::fromUtf8(argv[1]);
    }

    SplashWindow w(logo_path);
    w.show();
    return app.exec();
}
