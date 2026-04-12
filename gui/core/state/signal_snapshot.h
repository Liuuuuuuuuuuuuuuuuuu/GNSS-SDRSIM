#ifndef SIGNAL_SNAPSHOT_H
#define SIGNAL_SNAPSHOT_H

#include <cstdint>
#include <vector>

extern "C" {
#include "globals.h"
}

struct SatPoint {
    int prn;
    uint8_t is_gps;
    double lat_deg;
    double lon_deg;
};

struct TimeInfo {
    int bdt_week;
    double bdt_sow;
    int gpst_week;
    double gpst_sow;
    char utc_label[64];
    char bdt_label[64];
};

struct SpectrumSnapshot {
    float db[GUI_SPECTRUM_BINS];
    float rel_db[GUI_SPECTRUM_BINS];
    int16_t time_iq[2 * GUI_TIME_MON_SAMPLES];
    float time_i[GUI_TIME_MON_SAMPLES];
    float time_q[GUI_TIME_MON_SAMPLES];
    int bins;
    int valid;
    int time_samples;
    int time_valid;
};

void build_time_info(TimeInfo *ti);
void compute_sat_points(std::vector<SatPoint> &out, int week, double sow, int signal_mode);
void fetch_spectrum_snapshot(SpectrumSnapshot *snap);
void rel_db_to_rgb(float rel_db, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
