#include "dji_nfz.h"
#include "gui/nfz/dji_nfz_utils.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <cmath>

DjiNfzManager::DjiNfzManager(QObject* parent, std::function<void()> on_update)
    : on_update_cb_(on_update) 
{
    net_ = new QNetworkAccessManager(parent);
    timer_ = new QTimer(parent);
    timer_->setSingleShot(true); // 設定為單次回呼 (防抖用)
    
    QObject::connect(timer_, &QTimer::timeout, [this]() {
        this->execute_fetch();
    });

    QObject::connect(net_, &QNetworkAccessManager::finished, [this](QNetworkReply* reply) {
        this->on_reply(reply);
    });
}

void DjiNfzManager::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        timer_->stop();
        zones_.clear(); // 關閉時清空畫面
    } else {
        timer_->start(0); // 開啟時立刻觸發抓取
    }
}

void DjiNfzManager::trigger_fetch(double left_lon, double right_lon, double top_lat, double bottom_lat, int zoom) {
    if (!enabled_) return;
    cur_left_ = left_lon;
    cur_right_ = right_lon;
    cur_top_ = top_lat;
    cur_bottom_ = bottom_lat;
    cur_zoom_ = zoom;
    
    timer_->start(80); // 80 ms debounce — fast enough to feel immediate while still batching rapid zoom steps
}

void DjiNfzManager::execute_fetch() {
    QUrl url("https://flysafe-api.dji.com/api/qep/geo/feedback/areas/in_rectangle");
    QUrlQuery query;

    // ======== 新增範圍限制邏輯 ========
    // 計算當前視角的中心點
    double center_lat = (cur_top_ + cur_bottom_) / 2.0;
    double center_lon = (cur_left_ + cur_right_) / 2.0;
    
    double half_h = std::abs(cur_top_ - cur_bottom_) / 2.0;
    double half_w = std::abs(cur_right_ - cur_left_) / 2.0;

    // Low zoom requires a much larger query window; otherwise NFZ appears to
    // disappear when zooming out because only a tiny center area is fetched.
    double max_half_span_deg = 6.0;
    if (cur_zoom_ <= 4) {
        max_half_span_deg = 85.0;
    } else if (cur_zoom_ <= 6) {
        max_half_span_deg = 45.0;
    } else if (cur_zoom_ <= 8) {
        max_half_span_deg = 22.0;
    } else if (cur_zoom_ <= 10) {
        max_half_span_deg = 12.0;
    }
    if (half_h > max_half_span_deg) half_h = max_half_span_deg;
    if (half_w > max_half_span_deg) half_w = max_half_span_deg;

    double req_top = center_lat + half_h;
    double req_bottom = center_lat - half_h;
    double req_left = center_lon - half_w;
    double req_right = center_lon + half_w;
    // ======== ==================== ========

    // 下面的變數改成帶入限制過後的 req_* 變數
    query.addQueryItem("ltlat", QString::number(req_top, 'f', 6));
    query.addQueryItem("ltlng", QString::number(req_left, 'f', 6));
    query.addQueryItem("rblat", QString::number(req_bottom, 'f', 6));
    query.addQueryItem("rblng", QString::number(req_right, 'f', 6));
    
    query.addQueryItem("zones_mode", "flysafe_website");
    // 依照現場驗證，優先對齊 Mavic 3 的圖層回傳。
    query.addQueryItem("drone", "dji-mavic-3");
    query.addQueryItem("level", "0,1,2,3,7,8");

    url.setQuery(query);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Referer", "https://fly-safe.dji.com/");

    net_->get(req);
}

void DjiNfzManager::on_reply(QNetworkReply* reply) {
    if (!enabled_) {
        reply->deleteLater();
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (doc.isObject()) {
            const QJsonArray areas = doc.object().value("data").toObject().value("areas").toArray();
            if (!areas.isEmpty()) {
                std::vector<DjiNfzZone> parsed_zones;
                parsed_zones.reserve((size_t)areas.size() * 2);

                for (int i = 0; i < areas.size(); ++i) {
                    const QJsonObject area = areas.at(i).toObject();
                    const QString area_name = area.value("name").toString();
                    const int area_level = area.value("level").toInt(2);

                    const QJsonArray sub_areas = area.value("sub_areas").toArray();
                    if (!sub_areas.isEmpty()) {
                        // 若有 sub_areas，優先使用子區塊幾何，避免父層大圓與子層跑道膠囊重疊。
                        for (int j = 0; j < sub_areas.size(); ++j) {
                            if (!sub_areas.at(j).isObject()) continue;
                            dji_nfz_parse_zone_geometry(sub_areas.at(j).toObject(), area_name,
                                                        area_level, &parsed_zones);
                        }
                    } else {
                        dji_nfz_parse_zone_geometry(area, area_name, area_level, &parsed_zones);
                    }
                }

                if (!parsed_zones.empty()) {
                    zones_.swap(parsed_zones);
                    if (on_update_cb_) on_update_cb_();
                }
            }
        }
    }
    reply->deleteLater();
}