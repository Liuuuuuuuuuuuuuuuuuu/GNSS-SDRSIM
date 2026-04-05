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
    int interference_selection; // -1: none, 0: spoof, 1: jam
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
    bool spoof_allowed;
    bool running_ui;
    bool llh_ready;
    bool show_detailed_ctrl; // <-- 新增此變數記錄頁籤狀態
    char rinex_name[256];
};

#endif