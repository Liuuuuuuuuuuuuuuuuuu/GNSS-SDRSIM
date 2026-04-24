#ifndef DJI_DETECT_H
#define DJI_DETECT_H

#include <functional>
#include <mutex>
#include <QString>
#include <QMap>

#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class QObject;
class QJsonObject;
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
    QString essid;
    QString security;
    int channel = 0;
    quint64 beacon_count = 0;
    quint64 data_count = 0;
    double confidence = 0.0;
    double rssi_dbm = 0.0;
    double bearing_deg = 0.0;
    double distance_m = 0.0;
    bool has_rssi = false;
    bool has_bearing = false;
    bool has_distance = false;
    bool has_geo = false;
    double drone_lat = 0.0;
    double drone_lon = 0.0;
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

struct DjiRidBridgeSnapshot {
    bool has_sample = false;
    QString protocol;
    QString bridge;
    QString source;
    QString iface_or_hci;
    QString role;
    int channel = -1;
    QString profile;
    quint64 rx = 0;
    quint64 oui = 0;
    quint64 odid = 0;
    quint64 loc = 0;
    quint64 dropped = 0;
    double hit_rate = 0.0;
    unsigned recovery_count = 0;
    unsigned idle_windows = 0;
    unsigned no_signature_windows = 0;
    qint64 last_recovery_ms = 0;
    qint64 last_update_ms = 0;
    QString startup_state;
    QString self_check;
    QString health_status;
    QString health_detail;
};

struct DjiRidRuntimeSnapshot {
    DjiRidBridgeSnapshot wifi;
    DjiRidBridgeSnapshot ble;
    QMap<QString, DjiRidBridgeSnapshot> all_wifi; // keyed by iface name
    QMap<QString, DjiRidBridgeSnapshot> all_ble;  // keyed by hci name
};

struct DjiRidIdentityLockSnapshot {
    bool has_lock = false;
    QString protocol;
    QString source;
    QString remote_id_id;
    QString mac;
    double drone_lat = 0.0;
    double drone_lon = 0.0;
    bool has_operator = false;
    double operator_lat = 0.0;
    double operator_lon = 0.0;
    bool has_bearing = false;
    double bearing_deg = 0.0;
    bool has_distance = false;
    double distance_m = 0.0;
    int filter_samples = 0;
    qint64 last_seen_ms = 0;
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

    // Blacklist management for hard blocking (IDs or OUI prefixes, lower-case).
    void set_blacklist_csv(const QString& csv);
    void add_blacklist_id(const QString& id_or_prefix);
    void clear_blacklist();
    std::vector<QString> blacklist_items() const;

    // Snapshot for control-center UI rendering.
    std::vector<DjiDetectTargetSnapshot> targets_snapshot() const;

    // Latest RID packet diagnostic snapshot for on-screen field debugging.
    DjiRidDiagSnapshot last_rid_diag_snapshot() const;

    // Latest Wi-Fi / BLE bridge runtime health snapshots.
    DjiRidRuntimeSnapshot last_rid_runtime_snapshot() const;

    // Last valid RID identity+coordinate lock for GUI-only field operation.
    DjiRidIdentityLockSnapshot last_rid_identity_lock_snapshot() const;

    // Anonymous AoA target injection: when AoA module detects a strong, stable 2.4 GHz signal
    // without decodable OpenDroneID, inject synthetic "anonymous" threat with given bearing,
    // distance, and confidence (typically 0.75). Used by rid_rx_aoa_report() and similar.
    void inject_aoa_anonymous_target(double bearing_deg, double distance_m, double confidence_score);

private:
    struct TargetEntry {
        QString device_id;
        QString mac;
        QString source;
        QString vendor;
        QString model;
        QString oui;
        QString essid;
        QString security;
        int channel = 0;
        quint64 beacon_count = 0;
        quint64 data_count = 0;
        double confidence = 0.0;
        double rssi_dbm = 0.0;
        double bearing_deg = 0.0;
        double distance_m = 0.0;
        bool has_rssi = false;
        bool has_bearing = false;
        bool has_distance = false;
        bool has_geo = false;
        double drone_lat = 0.0;
        double drone_lon = 0.0;
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
        struct GeoHistEntry {
            qint64 ts_ms = 0;
            double lat = 0.0;
            double lon = 0.0;
        };
        static constexpr int kPosHistMax = 8;
        std::deque<PosHistEntry> pos_history;
        std::deque<GeoHistEntry> geo_history;
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
    void update_bridge_runtime_locked(const QJsonObject& object);
    void update_identity_lock_locked(const QJsonObject& object,
                                    const QString& source,
                                    bool has_bearing,
                                    double bearing_deg,
                                    bool has_distance,
                                    double distance_m);
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
        bool match_blacklist(const QString& device_id,
                                                 const QString& oui,
                                                 const QString& mac,
                                                 const QString& remote_id) const;
    bool match_whitelist(const QString& device_id, const QString& oui,
                         bool whitelist_hint) const;

    bool enabled_ = true;
    bool manual_override_enabled_ = false;
    qint64 last_detect_ms_ = 0;
    int hold_timeout_ms_ = 15000;
    double detect_threshold_ = 0.70;
    DjiDetectStatus status_{};
    DjiRidDiagSnapshot last_rid_diag_{};
    DjiRidRuntimeSnapshot last_rid_runtime_{};
    DjiRidIdentityLockSnapshot last_rid_identity_lock_{};
    struct RidCoordSample {
        qint64 ts_ms = 0;
        double lat = 0.0;
        double lon = 0.0;
    };
    std::deque<RidCoordSample> rid_identity_samples_;
    QString rid_identity_key_;
    std::unordered_map<std::string, TargetEntry> targets_;
    std::unordered_set<std::string> blacklist_ids_;
    std::unordered_set<std::string> whitelist_ids_;
    mutable std::mutex mtx_;
    QUdpSocket* udp_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::function<void(const DjiDetectStatus&)> on_update_cb_;
};

#endif // DJI_DETECT_H