#include "gui/nfz/dji_detect.h"

#include <QByteArray>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <strings.h>
#include <string>

namespace {

// Convert polar position (bearing_deg, distance_m) to (North, East) offsets in metres.
static void polar_to_ne(double bearing_deg, double distance_m, double& out_n, double& out_e) {
    const double rad = bearing_deg * M_PI / 180.0;
    out_n = distance_m * std::cos(rad);
    out_e = distance_m * std::sin(rad);
}

// Distance weighting bonus: targets closer than 10m get +0.17 confidence boost,
// linearly decaying to 0 at 50m+ (represents "threat proximity" factor).
static double calc_distance_bonus(double distance_m) {
    if (distance_m <= 10.0) return 0.17;  // Max bonus: 65% + 17% = 82%
    if (distance_m >= 50.0) return 0.0;   // No bonus beyond 50m
    // Linear interpolation between 10m and 50m
    return 0.17 * (50.0 - distance_m) / 40.0;
}

static QString lower_trimmed(const QString& s) {
    return s.trimmed().toLower();
}

static QString normalize_device_id(const QString& id) {
    return lower_trimmed(id).replace('-', ':');
}

static QString normalize_whitelist_item(const QString& id_or_prefix) {
    return normalize_device_id(id_or_prefix);
}

static bool looks_like_dji_oui(const QString& mac_or_oui) {
    // Commonly seen DJI OUIs. Keep list small to reduce false positives.
    static const char* kKnownPrefixes[] = {
        "60:60:1f", "90:3a:e6", "34:d2:70", "38:1c:1a", "5c:f3:70", "78:88:6d"
    };
    const QString key = lower_trimmed(mac_or_oui).replace('-', ':');
    for (const char* p : kKnownPrefixes) {
        if (key.startsWith(QString::fromLatin1(p))) {
            return true;
        }
    }
    return false;
}


static double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static bool rid_diag_log_enabled() {
    const char* v = std::getenv("BDS_WIFI_RID_DIAG_LOG");
    if (!v || !v[0]) return false;
    if (strcmp(v, "0") == 0) return false;
    if (strcasecmp(v, "false") == 0) return false;
    if (strcasecmp(v, "no") == 0) return false;
    return true;
}

} // namespace

DjiDetectManager::DjiDetectManager(QObject* parent,
                                   std::function<void(const DjiDetectStatus&)> on_update)
    : on_update_cb_(std::move(on_update)) {
    udp_ = new QUdpSocket(parent);
    watchdog_ = new QTimer(parent);
    watchdog_->setInterval(250);

    // Localhost-only ingest for safety: external detector should publish to 127.0.0.1:39001.
    udp_->bind(QHostAddress::LocalHost, 39001,
               QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    QObject::connect(udp_, &QUdpSocket::readyRead, [this]() { on_ready_read(); });
    QObject::connect(watchdog_, &QTimer::timeout, [this]() { on_watchdog_tick(); });
    watchdog_->start();
}

void DjiDetectManager::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lk(mtx_);
    enabled_ = enabled;
    if (!enabled_) {
        targets_.clear();
        apply_status(false, 0.0, QStringLiteral("disabled"), QString(), QString(),
                     0, QString());
    }
}

void DjiDetectManager::set_manual_override(bool enabled,
                                           bool detected,
                                           double confidence,
                                           const QString& source) {
    std::lock_guard<std::mutex> lk(mtx_);
    manual_override_enabled_ = enabled;
    if (manual_override_enabled_) {
        targets_.clear();
        apply_status(detected, clamp01(confidence), source,
                     QStringLiteral("manual"), QStringLiteral("manual"),
                     detected ? 1 : 0,
                     detected ? QStringLiteral("manual") : QString());
    }
}

void DjiDetectManager::set_whitelist_csv(const QString& csv) {
    std::lock_guard<std::mutex> lk(mtx_);
    whitelist_ids_.clear();
    const QStringList toks = csv.split(',', Qt::SkipEmptyParts);
    for (const QString& t : toks) {
        const QString key = normalize_whitelist_item(t);
        if (!key.isEmpty()) {
            whitelist_ids_.insert(key.toStdString());
        }
    }
}

void DjiDetectManager::add_whitelist_id(const QString& id_or_prefix) {
    std::lock_guard<std::mutex> lk(mtx_);
    const QString key = normalize_whitelist_item(id_or_prefix);
    if (!key.isEmpty()) {
        whitelist_ids_.insert(key.toStdString());
    }
}

void DjiDetectManager::clear_whitelist() {
    std::lock_guard<std::mutex> lk(mtx_);
    whitelist_ids_.clear();
}

std::vector<QString> DjiDetectManager::whitelist_items() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<QString> out;
    out.reserve(whitelist_ids_.size());
    for (const auto& it : whitelist_ids_) {
        out.push_back(QString::fromStdString(it));
    }
    std::sort(out.begin(), out.end());
    return out;
}

void DjiDetectManager::inject_aoa_anonymous_target(double bearing_deg,
                                                   double distance_m,
                                                   double confidence_score) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled_ || manual_override_enabled_) {
        return;
    }

    const double norm_bearing = std::fmod(bearing_deg + 360.0, 360.0);
    const double norm_distance = std::max(0.0, distance_m);
    char aoa_id_buf[64] = {0};
    snprintf(aoa_id_buf, sizeof(aoa_id_buf), "aoa-anon-%03d-%04d",
             (int)norm_bearing,
             (int)std::min(9999.0, norm_distance * 10.0));
    const std::string device_id_std = std::string(aoa_id_buf);
    const QString device_id = QString::fromStdString(device_id_std);

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    TargetEntry& entry = targets_[device_id_std];
    entry.device_id = device_id;
    entry.source = QStringLiteral("aoa-passive");
    entry.vendor = QStringLiteral("unknown");
    entry.model = QStringLiteral("aoa-signal");
    entry.oui = QString();
    entry.confidence = clamp01(confidence_score);
    entry.rssi_dbm = 0.0;
    entry.has_rssi = false;
    entry.bearing_deg = norm_bearing;
    entry.has_bearing = true;
    entry.distance_m = norm_distance;
    entry.has_distance = true;
    entry.confirmed_dji = false;
    entry.whitelist_hint = false;
    entry.whitelisted = false;
    entry.last_seen_ms = now_ms;

    if (entry.has_distance && entry.distance_m > 0.0) {
        TargetEntry::PosHistEntry ph{now_ms, entry.bearing_deg, entry.distance_m};
        entry.pos_history.push_back(ph);
        if ((int)entry.pos_history.size() > TargetEntry::kPosHistMax) {
            entry.pos_history.pop_front();
        }
        if (entry.pos_history.size() >= 2) {
            const auto& first = entry.pos_history.front();
            const auto& last = entry.pos_history.back();
            const double dt_s = (last.ts_ms - first.ts_ms) / 1000.0;
            if (dt_s > 0.5) {
                double n0, e0, n1, e1;
                polar_to_ne(first.bearing_deg, first.distance_m, n0, e0);
                polar_to_ne(last.bearing_deg, last.distance_m, n1, e1);
                const double vn = (n1 - n0) / dt_s;
                const double ve = (e1 - e0) / dt_s;
                const double sp = std::sqrt(vn * vn + ve * ve);
                entry.speed_mps = sp;
                if (sp > 0.5) {
                    double hdg = std::atan2(ve, vn) * 180.0 / M_PI;
                    if (hdg < 0.0) hdg += 360.0;
                    entry.heading_deg = hdg;
                    entry.has_velocity = true;
                } else {
                    entry.has_velocity = false;
                }
            }
        }
    }

    refresh_status_from_targets(QStringLiteral("aoa-passive"));
}

std::vector<DjiDetectTargetSnapshot> DjiDetectManager::targets_snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<DjiDetectTargetSnapshot> out;
    out.reserve(targets_.size());
    for (const auto& kv : targets_) {
        const TargetEntry& e = kv.second;
        DjiDetectTargetSnapshot s;
        s.device_id = e.device_id;
        s.source = e.source;
        s.vendor = e.vendor;
        s.model = e.model;
        s.oui = e.oui;
        s.confidence = e.confidence;
        s.rssi_dbm = e.rssi_dbm;
        s.bearing_deg = e.bearing_deg;
        s.distance_m = e.distance_m;
        s.has_rssi = e.has_rssi;
        s.has_bearing = e.has_bearing;
        s.has_distance = e.has_distance;
        s.confirmed_dji = e.confirmed_dji;
        s.whitelisted = e.whitelisted;
        s.heading_deg = e.heading_deg;
        s.speed_mps = e.speed_mps;
        s.has_velocity = e.has_velocity;
        s.last_seen_ms = e.last_seen_ms;
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const DjiDetectTargetSnapshot& a,
                                         const DjiDetectTargetSnapshot& b) {
        if (a.whitelisted != b.whitelisted) {
            return !a.whitelisted && b.whitelisted;
        }
        if (a.confirmed_dji != b.confirmed_dji) {
            return a.confirmed_dji && !b.confirmed_dji;
        }
        return a.confidence > b.confidence;
    });
    return out;
}

DjiRidDiagSnapshot DjiDetectManager::last_rid_diag_snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return last_rid_diag_;
}

void DjiDetectManager::on_ready_read() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled_ || manual_override_enabled_ || !udp_) {
        return;
    }

    while (udp_->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize((int)udp_->pendingDatagramSize());
        QHostAddress sender;
        quint16 port = 0;
        const qint64 n = udp_->readDatagram(buf.data(), buf.size(), &sender, &port);
        (void)n;
        (void)port;

        // Accept local-loopback producer only.
        if (!(sender == QHostAddress::LocalHost || sender == QHostAddress::LocalHostIPv6)) {
            continue;
        }

        bool detected = false;
        double confidence = 0.0;
        QString source;
        QString vendor;
        QString model;
        QString device_id;
        QString oui;
        double rssi_dbm = 0.0;
        bool has_rssi = false;
        double bearing_deg = 0.0;
        bool has_bearing = false;
        double distance_m = 0.0;
        bool has_distance = false;
        bool whitelist_hint = false;
        if (!parse_payload(buf, &detected, &confidence, &source, &vendor,
                           &model, &device_id, &oui,
                           &rssi_dbm, &has_rssi,
                           &bearing_deg, &has_bearing,
                           &distance_m, &has_distance,
                           &whitelist_hint)) {
            continue;
        }

        const bool whitelisted = match_whitelist(device_id, oui, whitelist_hint);
        update_last_rid_diag_locked(buf, source, confidence,
                                    has_bearing, has_distance, distance_m,
                                    whitelisted);

        const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
        if (!device_id.isEmpty()) {
            TargetEntry& entry = targets_[device_id.toStdString()];
            entry.device_id = device_id;
            entry.source = source;
            entry.vendor = vendor;
            entry.model = model;
            entry.oui = oui;
            entry.confidence = clamp01(confidence);
            entry.rssi_dbm = rssi_dbm;
            entry.has_rssi = has_rssi;
            entry.bearing_deg = bearing_deg;
            entry.has_bearing = has_bearing;
            entry.distance_m = distance_m;
            entry.has_distance = has_distance;
            entry.confirmed_dji = detected;
            entry.whitelist_hint = whitelist_hint;
            entry.whitelisted = whitelisted;
            entry.last_seen_ms = now_ms;

            // --- Trajectory estimation -------------------------------------------
            if (has_bearing && has_distance && distance_m > 0.0) {
                TargetEntry::PosHistEntry ph{now_ms, bearing_deg, distance_m};
                entry.pos_history.push_back(ph);
                if ((int)entry.pos_history.size() > TargetEntry::kPosHistMax) {
                    entry.pos_history.pop_front();
                }
                // Need at least 2 samples spanning > 0.5 s.
                if (entry.pos_history.size() >= 2) {
                    const auto& first = entry.pos_history.front();
                    const auto& last  = entry.pos_history.back();
                    const double dt_s = (last.ts_ms - first.ts_ms) / 1000.0;
                    if (dt_s > 0.5) {
                        double n0, e0, n1, e1;
                        polar_to_ne(first.bearing_deg, first.distance_m, n0, e0);
                        polar_to_ne(last.bearing_deg,  last.distance_m,  n1, e1);
                        const double vn = (n1 - n0) / dt_s;
                        const double ve = (e1 - e0) / dt_s;
                        const double sp = std::sqrt(vn * vn + ve * ve);
                        entry.speed_mps = sp;
                        if (sp > 0.5) {
                            double hdg = std::atan2(ve, vn) * 180.0 / M_PI;
                            if (hdg < 0.0) hdg += 360.0;
                            entry.heading_deg = hdg;
                            entry.has_velocity = true;
                        } else {
                            entry.has_velocity = false;
                        }
                    }
                }
            }
        }

        if (detected && !device_id.isEmpty()) {
            last_detect_ms_ = now_ms;
        }

        refresh_status_from_targets(source);
    }
}

void DjiDetectManager::refresh_status_from_targets(const QString& fallback_source) {
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    for (auto it = targets_.begin(); it != targets_.end();) {
        if (now_ms - it->second.last_seen_ms > hold_timeout_ms_) {
            it = targets_.erase(it);
        } else {
            ++it;
        }
    }

    if (targets_.empty()) {
        apply_status(false, 0.0,
                     fallback_source.isEmpty() ? QStringLiteral("timeout") : fallback_source,
                     QString(), QString(), 0, QString());
        return;
    }

    std::vector<std::reference_wrapper<const TargetEntry>> hostile_confirmed;
    hostile_confirmed.reserve(targets_.size());
    for (const auto& kv : targets_) {
        const TargetEntry& t = kv.second;
        const bool aoa_anonymous_hostile =
            (t.source == QStringLiteral("aoa-passive")) &&
            t.has_distance && (t.distance_m > 0.0) && (t.distance_m <= 50.0) &&
            (t.confidence >= 0.75);
        const bool rid_hostile =
            (t.source == QStringLiteral("wifi-rid") ||
             t.source == QStringLiteral("wifi-nan-rid") ||
             t.source == QStringLiteral("bt-le-rid")) &&
            t.has_bearing && t.has_distance && (t.distance_m > 0.0) &&
            (t.distance_m <= 5000.0) && (t.confidence >= detect_threshold_);
        if ((t.confirmed_dji || aoa_anonymous_hostile || rid_hostile) && !t.whitelisted) {
            hostile_confirmed.push_back(std::cref(t));
        }
    }

    if (hostile_confirmed.empty()) {
        apply_status(false, 0.0,
                     fallback_source.isEmpty() ? QStringLiteral("filtered") : fallback_source,
                     QString(), QString(), 0, QString());
        return;
    }

    const TargetEntry* top = &hostile_confirmed.front().get();
    for (const auto& ref : hostile_confirmed) {
        const TargetEntry& cand = ref.get();
        if (cand.confidence > top->confidence ||
            (cand.confidence == top->confidence &&
             cand.last_seen_ms > top->last_seen_ms)) {
            top = &cand;
        }
    }

    apply_status(true, top->confidence,
                 top->source.isEmpty() ? fallback_source : top->source,
                 top->vendor, top->model,
                 (int)hostile_confirmed.size(), top->device_id);
}

void DjiDetectManager::on_watchdog_tick() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled_ || manual_override_enabled_) {
        return;
    }
    refresh_status_from_targets(QStringLiteral("timeout"));
}

void DjiDetectManager::apply_status(bool detected,
                                    double confidence,
                                    const QString& source,
                                    const QString& vendor,
                                    const QString& model,
                                    int target_count,
                                    const QString& top_device_id) {
    const double c = clamp01(confidence);
    const bool changed = (status_.detected != detected) ||
                         (std::abs(status_.confidence - c) > 1e-6) ||
                         (status_.source != source) ||
                         (status_.vendor != vendor) ||
                         (status_.model != model) ||
                         (status_.target_count != target_count) ||
                         (status_.top_device_id != top_device_id);
    if (!changed) {
        return;
    }

    status_.detected = detected;
    status_.confidence = c;
    status_.source = source;
    status_.vendor = vendor;
    status_.model = model;
    status_.target_count = std::max(0, target_count);
    status_.top_device_id = top_device_id;
    if (on_update_cb_) {
        on_update_cb_(status_);
    }
}

bool DjiDetectManager::parse_payload(const QByteArray& payload,
                                     bool* out_detected,
                                     double* out_confidence,
                                     QString* out_source,
                                     QString* out_vendor,
                                     QString* out_model,
                                     QString* out_device_id,
                                     QString* out_oui,
                                     double* out_rssi_dbm,
                                     bool* out_has_rssi,
                                     double* out_bearing_deg,
                                     bool* out_has_bearing,
                                     double* out_distance_m,
                                     bool* out_has_distance,
                                     bool* out_whitelist_hint) const {
    if (!out_detected || !out_confidence || !out_source || !out_vendor ||
        !out_model || !out_device_id || !out_oui || !out_rssi_dbm ||
        !out_has_rssi || !out_bearing_deg || !out_has_bearing ||
        !out_distance_m || !out_has_distance || !out_whitelist_hint) {
        return false;
    }

    *out_oui = QString();
    *out_rssi_dbm = 0.0;
    *out_has_rssi = false;
    *out_bearing_deg = 0.0;
    *out_has_bearing = false;
    *out_distance_m = 0.0;
    *out_has_distance = false;
    *out_whitelist_hint = false;

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        const QString vendor = lower_trimmed(o.value("vendor").toString());
        const QString manufacturer = lower_trimmed(o.value("manufacturer").toString());
        QString model = lower_trimmed(o.value("model").toString());
        if (model.isEmpty()) model = lower_trimmed(o.value("aircraft_model").toString());
        if (model.isEmpty()) model = lower_trimmed(o.value("drone_model").toString());
        if (model.isEmpty()) model = lower_trimmed(o.value("product").toString());
        const QString oui = lower_trimmed(o.value("oui").toString());
        const QString mac = normalize_device_id(o.value("mac").toString());
        const QString rid = normalize_device_id(o.value("remote_id_id").toString());
        const QString serial = normalize_device_id(o.value("serial").toString());
        const QString source = o.value("source").toString(QStringLiteral("udp-json"));
        const bool has_is_dji = o.contains("is_dji") && o.value("is_dji").isBool();
        const bool is_dji = has_is_dji ? o.value("is_dji").toBool() : false;
        const bool remote_id = o.value("remote_id").toBool(false);
        const double reported_conf = o.contains("confidence") ? o.value("confidence").toDouble(0.0) : 0.0;
        if (o.contains("rssi_dbm")) {
            *out_rssi_dbm = o.value("rssi_dbm").toDouble(0.0);
            *out_has_rssi = true;
        }
        if (o.contains("bearing_deg")) {
            *out_bearing_deg = o.value("bearing_deg").toDouble(0.0);
            *out_has_bearing = true;
        }
        if (o.contains("distance_m")) {
            *out_distance_m = o.value("distance_m").toDouble(0.0);
            *out_has_distance = true;
        }
        if (o.contains("whitelist") && o.value("whitelist").isBool()) {
            *out_whitelist_hint = o.value("whitelist").toBool(false);
        }

        bool dji_hint = is_dji || vendor.contains("dji") || manufacturer.contains("dji") || model.contains("dji");
        bool oui_hint = looks_like_dji_oui(oui) || looks_like_dji_oui(mac);
        const bool rid_source =
            (source == QStringLiteral("wifi-rid")) ||
            (source == QStringLiteral("wifi-nan-rid")) ||
            (source == QStringLiteral("bt-le-rid"));
        const bool rid_geom_valid =
            remote_id && *out_has_bearing && *out_has_distance &&
            (*out_distance_m > 0.0) && (*out_distance_m <= 5000.0);

        double score = clamp01(reported_conf);
        if (dji_hint) score = std::max(score, 0.68);
        if (oui_hint) score = std::max(score, 0.72);
        if (remote_id && (dji_hint || oui_hint)) score = std::max(score, 0.90);
        if (rid_source && rid_geom_valid) score = std::max(score, 0.82);
        
        // Distance weighting bonus: targets closer than 10m receive additional confidence boost
        // (represents proximity threat). E.g. 0.65 confidence at 10m → 0.82 final score.
        if (*out_has_distance && *out_distance_m > 0.0) {
            const double dist_bonus = calc_distance_bonus(*out_distance_m);
            score = clamp01(score + dist_bonus);
        }

        const bool detected = ((dji_hint || oui_hint) || (rid_source && rid_geom_valid)) &&
                      (score >= detect_threshold_);

        *out_detected = detected;
        *out_confidence = score;
        *out_source = source;
        *out_vendor = vendor.isEmpty() ? manufacturer : vendor;
        *out_model = model;
        if (!mac.isEmpty()) {
            *out_device_id = mac;
        } else if (!rid.isEmpty()) {
            *out_device_id = rid;
        } else if (!serial.isEmpty()) {
            *out_device_id = serial;
        } else {
            *out_device_id = normalize_device_id(source + "|" + model);
        }
        *out_oui = normalize_device_id(oui);
        return true;
    }

    // Backward-compatible plain payload: "DJI,0.92" or "DJI".
    const QString text = lower_trimmed(QString::fromUtf8(payload));
    if (text.isEmpty()) {
        return false;
    }
    const bool looks_dji = text.contains("dji");
    double score = looks_dji ? 0.75 : 0.0;
    const int comma = text.indexOf(',');
    if (comma > 0) {
        bool ok = false;
        const double v = text.mid(comma + 1).toDouble(&ok);
        if (ok) {
            score = clamp01(v);
        }
    }
    *out_detected = looks_dji && (score >= detect_threshold_);
    *out_confidence = score;
    *out_source = QStringLiteral("udp-text");
    *out_vendor = looks_dji ? QStringLiteral("dji") : QString();
    *out_model = QString();
    *out_device_id = normalize_device_id(QStringLiteral("udp-text|dji"));
    return true;
}

void DjiDetectManager::update_last_rid_diag_locked(const QByteArray& payload,
                                                   const QString& source,
                                                   double confidence,
                                                   bool has_bearing,
                                                   bool has_distance,
                                                   double distance_m,
                                                   bool whitelisted) {
    const bool is_rid_source =
        (source == QStringLiteral("wifi-rid")) ||
        (source == QStringLiteral("wifi-nan-rid")) ||
        (source == QStringLiteral("bt-le-rid"));
    if (!is_rid_source) {
        return;
    }

    DjiRidDiagSnapshot s;
    s.has_sample = true;
    s.protocol = (source == QStringLiteral("bt-le-rid"))
                     ? QStringLiteral("BLE")
                     : ((source == QStringLiteral("wifi-nan-rid"))
                            ? QStringLiteral("Wi-Fi NAN")
                            : QStringLiteral("Wi-Fi"));
    s.distance_m = distance_m;
    s.has_distance = has_distance && (distance_m > 0.0);

    bool latlon_ok = false;
    bool remote_id = false;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        remote_id = o.value("remote_id").toBool(false);
        if (o.contains("drone_lat") && o.contains("drone_lon")) {
            const double lat = o.value("drone_lat").toDouble(999.0);
            const double lon = o.value("drone_lon").toDouble(999.0);
            latlon_ok = std::isfinite(lat) && std::isfinite(lon) &&
                        (lat >= -90.0) && (lat <= 90.0) &&
                        (lon >= -180.0) && (lon <= 180.0) &&
                        !(std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9);
        }
    }
    s.geom_valid = remote_id && latlon_ok;

    const bool rid_hostile =
        has_bearing && has_distance && (distance_m > 0.0) &&
        (distance_m <= 5000.0) && (confidence >= detect_threshold_);
    if (whitelisted) {
        s.gate_status = QStringLiteral("BLOCKED_WHITELIST");
    } else if (rid_hostile) {
        s.gate_status = QStringLiteral("PASSED");
    } else {
        s.gate_status = QStringLiteral("BLOCKED_CONFIDENCE");
    }

    last_rid_diag_ = s;

    if (rid_diag_log_enabled()) {
        const QByteArray src = s.protocol.toUtf8();
        const QByteArray gate = s.gate_status.toUtf8();
        std::fprintf(stderr,
                     "[rid-diag] proto=%s dist=%s geom=%s gate=%s conf=%.2f\n",
                     src.isEmpty() ? "n/a" : src.constData(),
                     s.has_distance ? QString::number(s.distance_m, 'f', 1).toUtf8().constData() : "n/a",
                     s.geom_valid ? "YES" : "NO",
                     gate.isEmpty() ? "n/a" : gate.constData(),
                     confidence);
    }
}

bool DjiDetectManager::match_whitelist(const QString& device_id,
                                       const QString& oui,
                                       bool whitelist_hint) const {
    if (whitelist_hint) {
        return true;
    }
    const std::string did = normalize_whitelist_item(device_id).toStdString();
    const std::string pfx = normalize_whitelist_item(oui).toStdString();
    for (const auto& w : whitelist_ids_) {
        if (w.empty()) {
            continue;
        }
        if (!did.empty() && did.rfind(w, 0) == 0) {
            return true;
        }
        if (!pfx.empty() && pfx.rfind(w, 0) == 0) {
            return true;
        }
    }
    return false;
}