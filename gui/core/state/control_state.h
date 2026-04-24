#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <cstdint>

struct GuiControlState {
    double tx_gain;
    double gain;
    double fs_mhz;
    double target_cn0;
    double selected_h_m;
    double path_vmax_kmh;
    double path_accel_mps2;
    unsigned seed;
    int single_prn;
    int sat_mode;
    int interference_selection; // -1: none, 0: spoof, 1: jam, 2: crossbow
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
    bool crossbow_direction_confirmed;
    bool crossbow_distance_ok;
    bool crossbow_dji_detected;
    double crossbow_dji_confidence;
    bool crossbow_auto_jam_enabled; // backend signal path only; UI remains in crossbow
    bool crossbow_unlocked;
    bool show_detailed_ctrl; // <-- 新增此變數記錄頁籤狀態
    // 可展開面板狀態
    bool hover_lb_panel;  // 滑鼠懸停在左下面板
    bool hover_rb_panel;  // 滑鼠懸停在右下面板
    double panel_expand_progress[2];  // 展開進度 [0]=左下, [1]=右下
    char rinex_name_bds[256];
    char rinex_name_gps[256];
};

#endif