#include "gui/path_builder.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

extern "C" {
#include "path.h"
}

static std::atomic<uint32_t> g_path_file_serial(0);

static bool ensure_runtime_path_dir(std::string *out_dir)
{
    const char *dir = "./runtime_paths";
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) return false;
        if (out_dir) *out_dir = dir;
        return true;
    }
    if (mkdir(dir, 0755) != 0) return false;
    if (out_dir) *out_dir = dir;
    return true;
}

static bool fetch_osrm_route_polyline(double lat0_deg, double lon0_deg,
                                      double lat1_deg, double lon1_deg,
                                      std::vector<LonLat> *out_polyline)
{
    if (!out_polyline) return false;
    out_polyline->clear();

    QString url = QString("https://router.project-osrm.org/route/v1/driving/%1,%2;%3,%4?overview=full&geometries=geojson")
                      .arg(lon0_deg, 0, 'f', 7)
                      .arg(lat0_deg, 0, 'f', 7)
                      .arg(lon1_deg, 0, 'f', 7)
                      .arg(lat1_deg, 0, 'f', 7);

    QNetworkAccessManager net;
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "bds-sim-map-gui/1.0");

    QNetworkReply *reply = net.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return false;
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray routes = root.value("routes").toArray();
    if (routes.isEmpty() || !routes.at(0).isObject()) return false;

    QJsonObject route0 = routes.at(0).toObject();
    QJsonObject geom = route0.value("geometry").toObject();
    QJsonArray coords = geom.value("coordinates").toArray();
    if (coords.size() < 2) return false;

    out_polyline->reserve((size_t)coords.size());
    for (int i = 0; i < coords.size(); ++i) {
        QJsonArray pt = coords.at(i).toArray();
        if (pt.size() < 2) continue;
        double lon = pt.at(0).toDouble();
        double lat = pt.at(1).toDouble();
        out_polyline->push_back({lon, lat});
    }
    return out_polyline->size() >= 2;
}

bool build_segment_path_file(double lat0_deg, double lon0_deg,
                             double lat1_deg, double lon1_deg,
                             int mode,
                             double vmax_mps,
                             double accel_mps2,
                             const std::vector<LonLat> *plan_polyline,
                             char out_path[256],
                             std::vector<LonLat> *out_polyline)
{
    const double dist_m = distance_m_approx(lat0_deg, lon0_deg, lat1_deg, lon1_deg);
    if (!(dist_m > 0.5)) return false;

    std::string dir;
    if (!ensure_runtime_path_dir(&dir)) return false;

    uint32_t serial = g_path_file_serial.fetch_add(1) + 1;
    std::snprintf(out_path, 256, "%s/seg_%06u_%s.llh", dir.c_str(), serial,
                  (mode == PATH_MODE_LINE) ? "line" : "plan");

    const double lat_ref_rad = lat0_deg * M_PI / 180.0;
    const double m_per_deg_lat = 111320.0;
    const double m_per_deg_lon = std::max(1.0, 111320.0 * std::cos(lat_ref_rad));

    const double x1 = wrap_lon_delta_deg(lon1_deg - lon0_deg) * m_per_deg_lon;
    const double y1 = (lat1_deg - lat0_deg) * m_per_deg_lat;

    std::vector<std::pair<double, double>> geom;
    std::vector<LonLat> route_geo;
    if (mode == PATH_MODE_LINE) {
        geom.push_back({0.0, 0.0});
        geom.push_back({x1, y1});
        route_geo.push_back({wrap_lon_deg(lon0_deg), lat0_deg});
        route_geo.push_back({wrap_lon_deg(lon1_deg), lat1_deg});
    } else {
        if (plan_polyline && plan_polyline->size() >= 2) {
            route_geo = *plan_polyline;
        } else {
            if (!fetch_osrm_route_polyline(lat0_deg, lon0_deg, lat1_deg, lon1_deg, &route_geo)) {
                return false;
            }
        }

        geom.reserve(route_geo.size());
        for (const auto &pt : route_geo) {
            double dx = wrap_lon_delta_deg(pt.lon - lon0_deg) * m_per_deg_lon;
            double dy = (pt.lat - lat0_deg) * m_per_deg_lat;
            geom.push_back({dx, dy});
        }
    }

    if (geom.size() < 2) return false;
    if (out_polyline) *out_polyline = route_geo;

    std::vector<double> cum;
    cum.resize(geom.size(), 0.0);
    for (size_t i = 1; i < geom.size(); ++i) {
        double dx = geom[i].first - geom[i - 1].first;
        double dy = geom[i].second - geom[i - 1].second;
        cum[i] = cum[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    double total_len = cum.back();
    if (!(total_len > 0.5)) return false;

    double v_max = vmax_mps;
    if (v_max < 1.0) v_max = 1.0;
    if (v_max > 80.0) v_max = 80.0;

    double a = accel_mps2;
    if (a < 0.2) a = 0.2;
    if (a > 10.0) a = 10.0;
    const double t_acc_nom = v_max / a;
    const double d_acc_nom = 0.5 * a * t_acc_nom * t_acc_nom;
    double t_acc = t_acc_nom;
    double t_flat = 0.0;
    double v_peak = v_max;
    double d_acc = d_acc_nom;
    double d_flat = 0.0;

    if (total_len <= 2.0 * d_acc_nom) {
        v_peak = std::sqrt(total_len * a);
        t_acc = v_peak / a;
        d_acc = 0.5 * a * t_acc * t_acc;
        t_flat = 0.0;
        d_flat = 0.0;
    } else {
        d_flat = total_len - 2.0 * d_acc_nom;
        t_flat = d_flat / v_max;
    }

    double total_t = 2.0 * t_acc + t_flat;
    if (total_t < 1.0 / PATH_UPDATE_HZ) total_t = 1.0 / PATH_UPDATE_HZ;
    int n_samples = (int)std::ceil(total_t * PATH_UPDATE_HZ) + 1;
    if (n_samples < 2) n_samples = 2;

    FILE *fp = std::fopen(out_path, "w");
    if (!fp) return false;

    for (int i = 0; i < n_samples; ++i) {
        double t = (double)i / PATH_UPDATE_HZ;
        if (t > total_t) t = total_t;

        double s = 0.0;
        if (t <= t_acc) {
            s = 0.5 * a * t * t;
        } else if (t <= t_acc + t_flat) {
            s = d_acc + v_peak * (t - t_acc);
        } else {
            double td = t - (t_acc + t_flat);
            s = d_acc + d_flat + v_peak * td - 0.5 * a * td * td;
        }
        if (s < 0.0) s = 0.0;
        if (s > total_len) s = total_len;

        size_t seg = 1;
        while (seg < cum.size() && cum[seg] < s) ++seg;
        if (seg >= cum.size()) seg = cum.size() - 1;

        double s0 = cum[seg - 1];
        double s1 = cum[seg];
        double f = (s1 > s0) ? (s - s0) / (s1 - s0) : 0.0;

        double x = geom[seg - 1].first * (1.0 - f) + geom[seg].first * f;
        double y = geom[seg - 1].second * (1.0 - f) + geom[seg].second * f;

        double lat = lat0_deg + y / m_per_deg_lat;
        double lon = lon0_deg + x / m_per_deg_lon;
        lon = wrap_lon_deg(lon);

        std::fprintf(fp, "%.10f %.10f 0.0\n", lat, lon);
    }

    std::fclose(fp);
    return true;
}
