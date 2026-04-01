#include "gui/signal_snapshot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

extern "C" {
#include "bdssim.h"
#include "coord.h"
#include "globals.h"
#include "orbits.h"
}

extern "C" int utc_bdt_diff;
extern "C" int utc_gpst_diff;

void build_time_info(TimeInfo *ti)
{
    auto now_sys = std::chrono::system_clock::now();
    auto now_sec_tp = std::chrono::time_point_cast<std::chrono::seconds>(now_sys);
    std::time_t now_t = std::chrono::system_clock::to_time_t(now_sec_tp);
    double frac = std::chrono::duration<double>(now_sys - now_sec_tp).count();
    std::tm tm_utc;
    gmtime_r(&now_t, &tm_utc);

    std::snprintf(ti->utc_label, sizeof(ti->utc_label),
                  "UTC %04d/%02d/%02d %02d:%02d:%02d",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);

    const time_t bdt0 = 1136073600;
    const time_t week_sec = 604800;
    time_t diff_int = now_t - bdt0 + utc_bdt_diff;
    long week = (long)(diff_int / week_sec);
    long rem = (long)(diff_int % week_sec);
    if (rem < 0) {
        rem += week_sec;
        --week;
    }
    double sow = (double)rem + frac;

    ti->week = (int)week;
    ti->sow = sow;
    std::snprintf(ti->bdt_label, sizeof(ti->bdt_label), "BDT Week %d  SOW %.2f", ti->week, ti->sow);
}

static void bdt_to_gpst_week_sow(int bdt_week, double bdt_sow, int *gps_week, double *gps_sow)
{
    const double week_sec = 604800.0;
    const double bdt0 = 1136073600.0;
    const double gps0 = 315964800.0;

    double unix_sec = bdt0 + (double)bdt_week * week_sec + bdt_sow - (double)utc_bdt_diff;
    double gpst_sec = unix_sec - gps0 + (double)utc_gpst_diff;

    int w = (int)floor(gpst_sec / week_sec);
    double sow = gpst_sec - (double)w * week_sec;
    if (sow < 0.0) {
        sow += week_sec;
        --w;
    }

    if (gps_week) *gps_week = w;
    if (gps_sow) *gps_sow = sow;
}

void compute_sat_points(std::vector<SatPoint> &out, int week, double sow, int signal_mode)
{
    out.clear();

    if (signal_mode == SIG_MODE_GPS || signal_mode == SIG_MODE_MIXED) {
        int gps_week = week;
        double gps_sow = sow;
        bdt_to_gpst_week_sow(week, sow, &gps_week, &gps_sow);

        for (int prn = 1; prn <= 32; ++prn) {
            if (gps_eph[prn].prn == 0) continue;

            double xyz[3] = {0.0, 0.0, 0.0};
            double vel[3] = {0.0, 0.0, 0.0};
            double clk[2] = {0.0, 0.0};
            calc_gps_position_velocity(prn, gps_week, gps_sow, xyz, vel, clk);

            coord_t lla;
            ecef_to_lla(xyz, &lla);
            double lat_deg = lla.llh[0] * 180.0 / M_PI;
            double lon_deg = lla.llh[1] * 180.0 / M_PI;

            if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg)) continue;

            out.push_back({prn, 1u, lat_deg, lon_deg});
        }
    }

    if (signal_mode == SIG_MODE_BDS || signal_mode == SIG_MODE_MIXED) {
        for (int prn = 1; prn < MAX_SAT && prn <= prn_max; ++prn) {
            if (is_geo_prn(prn)) continue;
            if (eph[prn].prn == 0) continue;

            double xyz[3] = {0.0, 0.0, 0.0};
            double vel[3] = {0.0, 0.0, 0.0};
            double clk[2] = {0.0, 0.0};
            calc_sat_position_velocity(prn, week, sow, xyz, vel, clk);

            coord_t lla;
            ecef_to_lla(xyz, &lla);
            double lat_deg = lla.llh[0] * 180.0 / M_PI;
            double lon_deg = lla.llh[1] * 180.0 / M_PI;

            out.push_back({prn, 0u, lat_deg, lon_deg});
        }
    }
}

void fetch_spectrum_snapshot(SpectrumSnapshot *snap)
{
    if (!snap) return;

    snap->bins = 0;
    snap->valid = 0;
    snap->time_samples = 0;
    snap->time_valid = 0;

    for (int i = 0; i < GUI_SPECTRUM_BINS; ++i) {
        snap->db[i] = 0.0f;
        snap->rel_db[i] = 0.0f;
    }
    for (int i = 0; i < GUI_TIME_MON_SAMPLES; ++i) {
        snap->time_iq[2 * i] = 0;
        snap->time_iq[2 * i + 1] = 0;
        snap->time_i[i] = 0.0f;
        snap->time_q[i] = 0.0f;
    }

    pthread_mutex_lock(&g_gui_spectrum_mtx);
    int bins = g_gui_spectrum_bins;
    if (bins > GUI_SPECTRUM_BINS) bins = GUI_SPECTRUM_BINS;
    if (bins < 0) bins = 0;
    snap->bins = bins;
    snap->valid = g_gui_spectrum_valid;
    for (int i = 0; i < bins; ++i) snap->db[i] = g_gui_spectrum_db[i];

    int td_samples = g_gui_time_samples;
    if (td_samples > GUI_TIME_MON_SAMPLES) td_samples = GUI_TIME_MON_SAMPLES;
    if (td_samples < 0) td_samples = 0;
    snap->time_samples = td_samples;
    snap->time_valid = g_gui_time_valid;
    for (int i = 0; i < td_samples; ++i) {
        snap->time_iq[2 * i] = g_gui_time_iq[2 * i];
        snap->time_iq[2 * i + 1] = g_gui_time_iq[2 * i + 1];
    }
    pthread_mutex_unlock(&g_gui_spectrum_mtx);

    if (!snap->valid || snap->bins < 8) return;

    const float db_min = -30.0f;
    const float db_max = 0.0f;
    float peak_v = snap->db[0];
    for (int i = 1; i < snap->bins; ++i) {
        if (snap->db[i] > peak_v) peak_v = snap->db[i];
    }

    for (int i = 0; i < snap->bins; ++i) {
        float v = snap->db[i] - peak_v;
        if (v < db_min) v = db_min;
        if (v > db_max) v = db_max;
        snap->rel_db[i] = v;
    }

    if (!snap->time_valid || snap->time_samples < 8) return;

    for (int i = 0; i < snap->time_samples; ++i) {
        snap->time_i[i] = (float)snap->time_iq[2 * i] / 32760.0f;
        snap->time_q[i] = (float)snap->time_iq[2 * i + 1] / 32760.0f;
    }
}

void rel_db_to_rgb(float rel_db, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float t = (rel_db + 30.0f) / 30.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float rr = 0.0f;
    float gg = 0.0f;
    float bb = 0.0f;
    if (t < (1.0f / 3.0f)) {
        float u = t * 3.0f;
        rr = 0.0f;
        gg = 255.0f * u;
        bb = 160.0f + 95.0f * u;
    } else if (t < (2.0f / 3.0f)) {
        float u = (t - (1.0f / 3.0f)) * 3.0f;
        rr = 255.0f * u;
        gg = 255.0f;
        bb = 255.0f * (1.0f - u);
    } else {
        float u = (t - (2.0f / 3.0f)) * 3.0f;
        rr = 255.0f;
        gg = 255.0f * (1.0f - u);
        bb = 0.0f;
    }

    *r = (uint8_t)rr;
    *g = (uint8_t)gg;
    *b = (uint8_t)bb;
}
