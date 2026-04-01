#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <cstdint>

struct GuiControlState {
    double tx_gain;
    double gain;
    double fs_mhz;
    double target_cn0;
    double path_vmax_kmh;
    double path_accel_mps2;
    unsigned seed;
    int single_prn;
    int sat_mode;
    int max_ch;
    int single_candidates[64];
    int single_candidate_count;
    int single_candidate_idx;
    bool meo_only;
    uint8_t signal_mode;
    bool byte_output;
    bool iono_on;
    bool usrp_external_clk;
    bool interference_mode;
    bool running_ui;
    bool llh_ready;
    char rinex_name[256];
};

#endif
