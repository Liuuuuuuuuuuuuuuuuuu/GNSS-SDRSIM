#ifndef DJI_DETECT_H
#define DJI_DETECT_H

#include <functional>
#include <mutex>
#include <QString>

#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QObject;
class QUdpSocket;
class QTimer;

struct DjiDetectStatus {
    bool detected = false;
    double confidence = 0.0;
    QString source;
    QString vendor;
    QString model;
    int target_count = 0;
    QString top_device_id;
};

struct DjiDetectTargetSnapshot {
    QString device_id;
    QString source;
    QString vendor;
    QString model;
    QString oui;
    double confidence = 0.0;
    double rssi_dbm = 0.0;
    double bearing_deg = 0.0;
    double distance_m = 0.0;
    bool has_rssi = false;
    bool has_bearing = false;
    bool has_distance = false;
    bool confirmed_dji = false;
    bool whitelisted = false;
    // Estimated velocity / trajectory (requires ≥2 position samples)
    double heading_deg = 0.0;   // direction of movement (0=North, 90=East)
    double speed_mps = 0.0;     // ground speed estimate
    bool has_velocity = false;
    qint64 last_seen_ms = 0;
    // Spoof-following flag: set by higher-level crossbow logic
    bool spoof_following = false;
};

struct DjiRidDiagSnapshot {
    bool has_sample = false;
    QString protocol;
    double distance_m = 0.0;
    bool has_distance = false;
    bool geom_valid = false;
    QString gate_status;
};

class DjiDetectManager {
public:
    DjiDetectManager(QObject* parent, std::function<void(const DjiDetectStatus&)> on_update);
    ~DjiDetectManager() = default;

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

    const DjiDetectStatus& status() const { return status_; }

    // Manual override for test/validation: when enabled, incoming UDP is ignored.
    void set_manual_override(bool enabled, bool detected, double confidence, const QString& source);

    // Whitelist management for friendly devices (IDs or OUI prefixes, lower-case).
    void set_whitelist_csv(const QString& csv);
    void add_whitelist_id(const QString& id_or_prefix);
    void clear_whitelist();
    std::vector<QString> whitelist_items() const;

    // Snapshot for control-center UI rendering.
    std::vector<DjiDetectTargetSnapshot> targets_snapshot() const;

    // Latest RID packet diagnostic snapshot for on-screen field debugging.
    DjiRidDiagSnapshot last_rid_diag_snapshot() const;

    // Anonymous AoA target injection: when AoA module detects a strong, stable 2.4 GHz signal
    // without decodable OpenDroneID, inject synthetic "anonymous" threat with given bearing,
    // distance, and confidence (typically 0.75). Used by rid_rx_aoa_report() and similar.
    void inject_aoa_anonymous_target(double bearing_deg, double distance_m, double confidence_score);

private:
    struct TargetEntry {
        QString device_id;
        QString source;
        QString vendor;
        QString model;
        QString oui;
        double confidence = 0.0;
        double rssi_dbm = 0.0;
        double bearing_deg = 0.0;
        double distance_m = 0.0;
        bool has_rssi = false;
        bool has_bearing = false;
        bool has_distance = false;
        bool confirmed_dji = false;
        bool whitelist_hint = false;
        bool whitelisted = false;
        qint64 last_seen_ms = 0;

        // Trajectory estimation — ring buffer of recent (bearing, distance) pairs.
        struct PosHistEntry {
            qint64 ts_ms = 0;
            double bearing_deg = 0.0;
            double distance_m = 0.0;
        };
        static constexpr int kPosHistMax = 8;
        std::deque<PosHistEntry> pos_history;
        double heading_deg = 0.0;
        double speed_mps = 0.0;
        bool has_velocity = false;
    };

    void on_ready_read();
    void on_watchdog_tick();
    void apply_status(bool detected, double confidence, const QString& source,
                      const QString& vendor, const QString& model,
                      int target_count, const QString& top_device_id);
    void refresh_status_from_targets(const QString& fallback_source = QString());
    void update_last_rid_diag_locked(const QByteArray& payload,
                                     const QString& source,
                                     double confidence,
                                     bool has_bearing,
                                     bool has_distance,
                                     double distance_m,
                                     bool whitelisted);
    bool parse_payload(const QByteArray& payload,
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
                       bool* out_whitelist_hint) const;
    bool match_whitelist(const QString& device_id, const QString& oui,
                         bool whitelist_hint) const;

    bool enabled_ = true;
    bool manual_override_enabled_ = false;
    qint64 last_detect_ms_ = 0;
    int hold_timeout_ms_ = 2500;
    double detect_threshold_ = 0.70;
    DjiDetectStatus status_{};
    DjiRidDiagSnapshot last_rid_diag_{};
    std::unordered_map<std::string, TargetEntry> targets_;
    std::unordered_set<std::string> whitelist_ids_;
    mutable std::mutex mtx_;
    QUdpSocket* udp_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::function<void(const DjiDetectStatus&)> on_update_cb_;
};

#endif // DJI_DETECT_H