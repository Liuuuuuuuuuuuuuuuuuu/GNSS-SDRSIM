#ifndef DJI_NFZ_H
#define DJI_NFZ_H

#include <vector>
#include <QString>
#include <QPainter>
#include <QRect>
#include <functional>

// 宣告 Qt 類別 (減少 include 負擔)
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QObject;

enum class DjiNfzType { POLYGON, CIRCLE };

struct DjiLonLat {
    double lon;
    double lat;
};

// 單一禁航區結構
struct DjiNfzZone {
    QString name;
    DjiNfzType type;
    int level;
    std::vector<DjiLonLat> polygon;
    double center_lat;
    double center_lon;
    double radius_m;
};

// 獨立的 API 管理器類別
class DjiNfzManager {
public:
    // parent 用來綁定生命週期與 Qt 事件迴圈，on_update 為資料更新時的回呼函式
    DjiNfzManager(QObject* parent, std::function<void()> on_update);
    ~DjiNfzManager() = default;

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }

    // 觸發抓取 (內部自帶 0.5 秒防抖，避免拖曳時瘋狂發送請求)
    void trigger_fetch(double left_lon, double right_lon, double top_lat, double bottom_lat, int zoom);
    
    const std::vector<DjiNfzZone>& get_zones() const { return zones_; }

private:
    void execute_fetch();
    void on_reply(QNetworkReply* reply);

    bool enabled_ = false;
    std::vector<DjiNfzZone> zones_;
    QNetworkAccessManager* net_ = nullptr;
    QTimer* timer_ = nullptr;
    std::function<void()> on_update_cb_;

    double cur_left_ = 0, cur_right_ = 0, cur_top_ = 0, cur_bottom_ = 0;
    int cur_zoom_ = 0;
};

// 繪製工具函式 (解耦：將經緯度轉換交由外部 lambda 處理)
void dji_nfz_draw(QPainter& p, const QRect& panel, const std::vector<DjiNfzZone>& zones, int zoom,
                  std::function<bool(double lat, double lon, QPoint* out)> coord_to_screen_fn);

#endif // DJI_NFZ_H