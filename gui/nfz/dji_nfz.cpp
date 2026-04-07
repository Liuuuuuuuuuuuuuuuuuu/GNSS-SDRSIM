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
#include <QColor>
#include <algorithm>
#include <cmath>
#include <QPainterPath>  // <== 加入這一行

namespace {

int nfz_draw_weight(int level) {
    // GEO 分層：先畫外層授權區，再畫限高/警示，最後畫核心禁飛紅區
    if (level == 1) return 1;
    if (level == 8) return 2;
    if (level == 2) return 3;
    return 0;
}

double ring_area_abs(const std::vector<DjiLonLat>& pts) {
    if (pts.size() < 3) return 0.0;
    double s = 0.0;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        s += (pts[j].lon * pts[i].lat) - (pts[i].lon * pts[j].lat);
    }
    return std::abs(s) * 0.5;
}

double zone_area_score(const DjiNfzZone& z) {
    if (z.type == DjiNfzType::CIRCLE) {
        return M_PI * z.radius_m * z.radius_m;
    }
    if (!z.rings.empty()) {
        return ring_area_abs(z.rings[0].points);
    }
    return 0.0;
}

bool is_coordinate_pair(const QJsonArray& arr) {
    return arr.size() >= 2 && arr.at(0).isDouble() && arr.at(1).isDouble();
}

bool parse_ring_points(const QJsonValue& ring_val, DjiPolygonRing* out_ring) {
    if (!out_ring || !ring_val.isArray()) return false;
    const QJsonArray ring_arr = ring_val.toArray();
    for (int i = 0; i < ring_arr.size(); ++i) {
        const QJsonValue pt_val = ring_arr.at(i);
        if (pt_val.isObject()) {
            const QJsonObject pt_obj = pt_val.toObject();
            if (pt_obj.contains("lng") && pt_obj.contains("lat")) {
                out_ring->points.push_back({pt_obj.value("lng").toDouble(),
                                            pt_obj.value("lat").toDouble()});
            }
            continue;
        }
        if (pt_val.isArray()) {
            const QJsonArray pt_arr = pt_val.toArray();
            if (is_coordinate_pair(pt_arr)) {
                out_ring->points.push_back({pt_arr.at(0).toDouble(),
                                            pt_arr.at(1).toDouble()});
            }
        }
    }
    return out_ring->points.size() >= 3;
}

void append_polygon_from_value(const QJsonValue& poly_val, std::vector<DjiPolygonRing>* out_rings) {
    if (!out_rings || !poly_val.isArray()) return;
    const QJsonArray poly_arr = poly_val.toArray();
    if (poly_arr.isEmpty()) return;

    // 支援兩種格式：
    // 1) [ [lng, lat], ... ] (單一外環)
    // 2) [ [ [lng, lat], ... ], [ [lng, lat], ... ] ] (外環 + 內環)
    const QJsonValue first = poly_arr.at(0);
    if (first.isArray() && !is_coordinate_pair(first.toArray())) {
        for (int i = 0; i < poly_arr.size(); ++i) {
            DjiPolygonRing ring;
            ring.is_outer = (i == 0);
            if (parse_ring_points(poly_arr.at(i), &ring)) {
                out_rings->push_back(std::move(ring));
            }
        }
        return;
    }

    DjiPolygonRing outer;
    outer.is_outer = true;
    if (parse_ring_points(poly_val, &outer)) {
        out_rings->push_back(std::move(outer));
    }
}

void parse_zone_geometry(const QJsonObject& src,
                         const QString& zone_name,
                         int fallback_level,
                         std::vector<DjiNfzZone>* out_zones) {
    if (!out_zones) return;

    DjiNfzZone nfz;
    nfz.name = zone_name;
    nfz.level = src.value("level").toInt(fallback_level);
    nfz.color_hex = src.value("color").toString();

    const QJsonValue poly_val = src.value("polygon_points");
    if (poly_val.isArray() && !poly_val.toArray().isEmpty()) {
        nfz.type = DjiNfzType::POLYGON;
        append_polygon_from_value(poly_val, &nfz.rings);

        QJsonValue inner_val = src.value("inner_rings");
        if (!inner_val.isArray()) inner_val = src.value("holes");
        if (!inner_val.isArray()) inner_val = src.value("inner_polygons");
        if (inner_val.isArray()) {
            const QJsonArray inner_arr = inner_val.toArray();
            for (int i = 0; i < inner_arr.size(); ++i) {
                DjiPolygonRing inner_ring;
                inner_ring.is_outer = false;
                if (parse_ring_points(inner_arr.at(i), &inner_ring)) {
                    nfz.rings.push_back(std::move(inner_ring));
                }
            }
        }

        if (!nfz.rings.empty()) {
            out_zones->push_back(std::move(nfz));
            return;
        }
    }

    const double radius = src.value("radius").toDouble();
    if (radius > 0.0) {
        nfz.type = DjiNfzType::CIRCLE;
        nfz.center_lat = src.value("lat").toDouble();
        nfz.center_lon = src.value("lng").toDouble();
        nfz.radius_m = radius;
        out_zones->push_back(std::move(nfz));
    }
}

void add_ring_to_path(QPainterPath& path,
                      const DjiPolygonRing& ring,
                      std::function<bool(double, double, QPoint*)> coord_to_screen_fn,
                      bool smooth_edges) {
    if (ring.points.size() < 3) return;

    std::vector<QPointF> screen_pts;
    screen_pts.reserve(ring.points.size());
    for (const auto& pt : ring.points) {
        QPoint scr;
        if (coord_to_screen_fn(pt.lat, pt.lon, &scr)) {
            screen_pts.push_back(QPointF(scr));
        }
    }
    if (screen_pts.size() < 3) return;

    // 常見 GeoJSON 會在最後重複第一點做閉合；平滑前先移除避免形狀扭曲。
    if (screen_pts.size() >= 4) {
        const QPointF& first = screen_pts.front();
        const QPointF& last = screen_pts.back();
        if (std::abs(first.x() - last.x()) < 0.5 && std::abs(first.y() - last.y()) < 0.5) {
            screen_pts.pop_back();
        }
    }
    if (screen_pts.size() < 3) return;

    // 平滑策略：使用 midpoint + quadTo，視覺上更接近遙控器上的糖果/球場邊緣。
    // 低頂點數（如矩形/梯形）不平滑，保持幾何原貌。
    if (smooth_edges && screen_pts.size() >= 7) {
        const int n = (int)screen_pts.size();
        QPointF p0 = screen_pts[0];
        QPointF p1 = screen_pts[1];
        QPointF start_mid((p0.x() + p1.x()) * 0.5, (p0.y() + p1.y()) * 0.5);
        path.moveTo(start_mid);

        for (int i = 0; i < n; ++i) {
            const QPointF& ctrl = screen_pts[i];
            const QPointF& next = screen_pts[(i + 1) % n];
            QPointF mid((ctrl.x() + next.x()) * 0.5, (ctrl.y() + next.y()) * 0.5);
            path.quadTo(ctrl, mid);
        }
        path.closeSubpath();
        return;
    }

    path.moveTo(screen_pts[0]);
    for (size_t i = 1; i < screen_pts.size(); ++i) {
        path.lineTo(screen_pts[i]);
    }
    path.closeSubpath();
}

} // namespace
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
                            parse_zone_geometry(sub_areas.at(j).toObject(), area_name,
                                                area_level, &parsed_zones);
                        }
                    } else {
                        parse_zone_geometry(area, area_name, area_level, &parsed_zones);
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

void dji_nfz_draw(QPainter& p, const QRect& panel, const std::vector<DjiNfzZone>& zones, int zoom,
                  std::function<bool(double, double, QPoint*)> coord_to_screen_fn) 
{
    (void)panel; // 消除 unused parameter 警告
    std::vector<DjiNfzZone> sorted_zones = zones;
    std::sort(sorted_zones.begin(), sorted_zones.end(), [](const DjiNfzZone& a, const DjiNfzZone& b) {
        const int wa = nfz_draw_weight(a.level);
        const int wb = nfz_draw_weight(b.level);
        if (wa != wb) return wa < wb;
        // 同層級時，先畫較大區塊，讓小核心區塊疊在上方。
        return zone_area_score(a) > zone_area_score(b);
    });

    // 放大倍率夠大時啟用平滑邊緣，避免低 zoom 過度扭曲。
    const bool smooth_edges = (zoom >= 10);
    for (const auto &nfz : sorted_zones) {
        QColor stroke;
        QColor fill;
        QColor server_color(nfz.color_hex);
        if (server_color.isValid()) {
            stroke = server_color;
            fill = server_color;
            stroke.setAlpha(230);
            fill.setAlpha(78);
        } else if (nfz.level == 2) { // 核心禁飛紅區
            stroke = QColor(220, 38, 38, 235);
            fill = QColor(220, 38, 38, 88);
        } else if (nfz.level == 8) { // 限高/警示區（黃）
            stroke = QColor(217, 119, 6, 230);
            fill = QColor(245, 158, 11, 75);
        } else { // 授權區（藍）
            stroke = QColor(37, 99, 235, 230);
            fill = QColor(59, 130, 246, 60);
        }
        p.setPen(QPen(stroke, 2, Qt::SolidLine));
        p.setBrush(QBrush(fill));

        if (nfz.type == DjiNfzType::POLYGON) {
            // 支援複雜多邊形（含內環/洞）
            // 使用 OddEvenFill 規則讓內環顯示為洞
            QPainterPath path;
            path.setFillRule(Qt::OddEvenFill);  // 關鍵：用此規則處理洞
            
            for (const auto &ring : nfz.rings) {
                add_ring_to_path(path, ring, coord_to_screen_fn, smooth_edges);
            }
            
            if (!path.isEmpty()) {
                p.drawPath(path); 
            }
        } else if (nfz.type == DjiNfzType::CIRCLE) {
            QPoint center_pt;
            if (coord_to_screen_fn(nfz.center_lat, nfz.center_lon, &center_pt)) {
                // 將地球公尺轉換為像素
                double m_per_px = 156543.03392 * std::cos(nfz.center_lat * M_PI / 180.0) / std::pow(2, zoom);
                int r_px = (int)std::round(nfz.radius_m / m_per_px);
                
                // 就算縮到超小，保底也要畫 3 個像素
                if (r_px < 3) r_px = 3;

                // 使用傳統圓形渲染
                p.drawEllipse(center_pt, r_px, r_px);
            }
        }
    }
}