#include "dji_nfz.h"
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
#include <QPainterPath>  // <== 加入這一行
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
    
    timer_->start(200); // 延遲 0.5 秒發送，減少無謂請求
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
    
    // 強制限制：無論縮放到多小(Zoom 5)，向伺服器請求的長寬半徑最大不超過 5.0 度
    // ======== 把 3.0 改成 5.0 ========
    if (half_h > 5.0) half_h = 5.0; 
    if (half_w > 5.0) half_w = 5.0;
    // ======== ======================= ========

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
    query.addQueryItem("drone", "dji-mavic-3");
    query.addQueryItem("level", "0,1,2,3,7,8,10"); 

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
            QJsonArray areas = doc.object().value("data").toObject().value("areas").toArray();
            
            // 只有當伺服器真的有吐資料回來時才清空 (防呆：避免 BBox 過大回傳 0 筆時畫面變白)
            // 只有當伺服器真的有吐資料回來時才處理
            if (areas.size() > 0) {
                
                // ======== 1. 把這行註解掉，不要再每次都清空畫面了 ========
                // zones_.clear(); 
                
                // ======== 2. 加入保護機制，避免記憶體塞爆 (超過 2000 個禁航區就清空重置) ========
                if (zones_.size() > 2000) {
                    zones_.clear();
                }

                for (int i = 0; i < areas.size(); ++i) {
                    QJsonObject area = areas.at(i).toObject();
                    DjiNfzZone nfz;
                    nfz.name = area.value("name").toString();
                    nfz.level = area.value("level").toInt(2);

                    // ======== 3. 加入「去重複」邏輯 ========
                    // 檢查畫面上是不是已經有這個禁航區了，避免每次拖曳都重複添加
                    bool is_dup = false;
                    for (const auto& z : zones_) {
                        if (z.name == nfz.name && z.level == nfz.level) {
                            is_dup = true;
                            break;
                        }
                    }
                    if (is_dup) continue; // 如果已經存在，就跳過不處理
                    // ======== ======================= ========

                    // ===== (以下保留你原本解析 polygon 與 circle 的程式碼，完全不用動) =====
                    QJsonValue poly_val = area.value("polygon_points");
                    if (poly_val.isArray() && !poly_val.toArray().isEmpty()) {
                        nfz.type = DjiNfzType::POLYGON;
                        QJsonArray points = poly_val.toArray();
                        for (int j = 0; j < points.size(); ++j) {
                            QJsonValue pt_val = points.at(j);
                            if (pt_val.isObject()) {
                                nfz.polygon.push_back({pt_val.toObject().value("lng").toDouble(), pt_val.toObject().value("lat").toDouble()});
                            } else if (pt_val.isArray() && pt_val.toArray().size() >= 2) {
                                nfz.polygon.push_back({pt_val.toArray().at(0).toDouble(), pt_val.toArray().at(1).toDouble()});
                            }
                        }
                        if (nfz.polygon.size() >= 3) zones_.push_back(nfz);
                    } else if (area.contains("radius") && area.value("radius").toDouble() > 0) {
                        nfz.type = DjiNfzType::CIRCLE;
                        nfz.center_lat = area.value("lat").toDouble();
                        nfz.center_lon = area.value("lng").toDouble();
                        nfz.radius_m = area.value("radius").toDouble();
                        zones_.push_back(nfz);
                    }
                }
                // 通知地圖進行重繪
                if (on_update_cb_) on_update_cb_();
            }
        }
    }
    reply->deleteLater();
}

void dji_nfz_draw(QPainter& p, const QRect& panel, const std::vector<DjiNfzZone>& zones, int zoom,
                  std::function<bool(double, double, QPoint*)> coord_to_screen_fn) 
{
    (void)panel; // 消除 unused parameter 警告
    for (const auto &nfz : zones) {
        if (nfz.level == 2 || nfz.level == 8) { // 禁飛紅區
            p.setPen(QPen(QColor(255, 0, 255, 240), 2, Qt::SolidLine));
            p.setBrush(QBrush(QColor(255, 0, 255, 80)));
        } else { // 授權藍區或警告黃區
            p.setPen(QPen(QColor(59, 130, 246, 240), 2, Qt::SolidLine));
            p.setBrush(QBrush(QColor(59, 130, 246, 60)));
        }

        if (nfz.type == DjiNfzType::POLYGON) {
            QPainterPath path;
            bool first = true;
            for (const auto &pt : nfz.polygon) {
                QPoint scr;
                if (coord_to_screen_fn(pt.lat, pt.lon, &scr)) {
                    if (first) { path.moveTo(scr); first = false; } 
                    else { path.lineTo(scr); }
                }
            }
            if (!first) { 
                path.closeSubpath(); 
                p.drawPath(path); 
            }
        } else if (nfz.type == DjiNfzType::CIRCLE) {
            QPoint center_pt;
            if (coord_to_screen_fn(nfz.center_lat, nfz.center_lon, &center_pt)) {
                // 將地球公尺轉換為像素
                double m_per_px = 156543.03392 * std::cos(nfz.center_lat * M_PI / 180.0) / std::pow(2, zoom);
                int r_px = (int)std::round(nfz.radius_m / m_per_px);
                
                // ======== 新增這行：就算縮到超小，保底也要畫 3 個像素 ========
                if (r_px < 3) r_px = 3;
                // ======== ========================================== ========

                p.drawEllipse(center_pt, r_px, r_px);
            }
        }
    }
}