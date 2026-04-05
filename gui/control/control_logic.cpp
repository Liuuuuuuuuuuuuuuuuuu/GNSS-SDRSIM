#include "gui/control/control_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include "bdssim.h"
#include "globals.h"
#include "main_gui.h"
}

static inline double clamp_double(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline double mode_min_fs_hz(uint8_t signal_mode) {
    if (signal_mode == SIG_MODE_GPS) return 2.6e6;
    if (signal_mode == SIG_MODE_MIXED) return 20.8e6;
    return 5.2e6;
}

static inline double snap_fs_to_mode_grid_hz(double /*fs_hz*/, uint8_t signal_mode) {
    if (signal_mode == SIG_MODE_GPS) return 2.6e6;
    if (signal_mode == SIG_MODE_MIXED) return 20.8e6;
    return 5.2e6;
}

bool control_logic_handle_click(int x, int y, int win_width, int win_height, GuiControlState *ctrl, std::mutex *ctrl_mtx,
                                std::atomic<uint32_t> *start_req, std::atomic<uint32_t> *stop_req,
                                std::atomic<uint32_t> *exit_req, volatile int *runtime_abort) {
    if (!ctrl || !ctrl_mtx || !start_req || !stop_req || !exit_req || !runtime_abort) return false;

    std::lock_guard<std::mutex> lk(*ctrl_mtx);

    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo, ctrl->show_detailed_ctrl);
    if (!rect_hit(lo.panel, x, y)) return false;

    if (rect_hit(lo.btn_tab_simple, x, y)) {
        if (ctrl->show_detailed_ctrl) { ctrl->show_detailed_ctrl = false; return true; }
    }
    if (rect_hit(lo.btn_tab_detail, x, y)) {
        if (!ctrl->show_detailed_ctrl) { ctrl->show_detailed_ctrl = true; return true; }
    }
    if (rect_hit(lo.btn_exit, x, y)) {
        if (!ctrl->running_ui) { exit_req->fetch_add(1); return true; }
    }
    if (rect_hit(lo.btn_start, x, y)) {
        if (!ctrl->running_ui && ctrl->interference_selection >= 0) {
            if (ctrl->interference_selection == 0 && !ctrl->spoof_allowed) {
                map_gui_push_alert(2, "SPOOF mode requires a valid RINEX file. JAM only is available right now.");
                return true;
            }
            // Enter running UI immediately so STOP and monitor widgets appear
            // without waiting for the main loop to consume the start request.
            ctrl->running_ui = true;
            start_req->fetch_add(1);
            return true;
        }
    }

    int jam_idx = segmented_index_hit(lo.sw_jam, x, y, 2);
    if (jam_idx >= 0) {
        if (ctrl->running_ui) return false;
        if (jam_idx == 0) {
            if (!ctrl->spoof_allowed) {
                map_gui_push_alert(2, "SPOOF mode requires a valid RINEX file. JAM only is available right now.");
                return true;
            }
            ctrl->interference_selection = 0;
            ctrl->interference_mode = false;
            return true;
        }

        ctrl->interference_selection = 1;
        ctrl->interference_mode = true;
        if (ctrl->signal_mode == SIG_MODE_GPS && ctrl->fs_mhz < 2.6) {
            ctrl->fs_mhz = 2.6;
        }
        return true;
    }

    int sys_idx = segmented_index_hit(lo.sw_sys, x, y, 3);
    if (sys_idx >= 0) {
        if (ctrl->running_ui) return false;
        uint8_t next_mode = (sys_idx == 0) ? SIG_MODE_BDS : ((sys_idx == 1) ? SIG_MODE_MIXED : SIG_MODE_GPS);
        if (ctrl->signal_mode != next_mode) {
            ctrl->signal_mode = next_mode;
            ctrl->fs_mhz = mode_min_fs_hz(ctrl->signal_mode) / 1e6;

            if (ctrl->signal_mode == SIG_MODE_MIXED && ctrl->sat_mode == 0) {
                ctrl->sat_mode = 1;
            } else if (ctrl->signal_mode == SIG_MODE_GPS && ctrl->sat_mode == 2) {
                ctrl->sat_mode = 1;
            }
            return true;
        }
        return false;
    }

    bool spoof_mode = (ctrl->interference_selection == 0 && ctrl->spoof_allowed);
    bool jam_mode = (ctrl->interference_selection == 1);
    bool mode_any = (spoof_mode || jam_mode);

    if (ctrl->show_detailed_ctrl) {
        int fmt_idx = segmented_index_hit(lo.sw_fmt, x, y, 2);
        if (fmt_idx >= 0) {
            if (ctrl->running_ui || !mode_any) return false;
            ctrl->byte_output = (fmt_idx == 1);
            return true;
        }

        if (rect_hit(lo.cn0_slider, x, y)) {
            if (ctrl->running_ui || !mode_any) return false;
            double t = slider_ratio_hit(lo.cn0_slider, x);
            ctrl->target_cn0 = std::round((20.0 + (60.0 - 20.0) * t) * 2.0) / 2.0;
            return true;
        }
    }

    if (ctrl->running_ui) return false;

    if (ctrl->interference_selection == 1) {
        if (rect_hit(lo.tx_slider, x, y)) {
            double t = slider_ratio_hit(lo.tx_slider, x);
            ctrl->tx_gain = std::round((100.0 * t) * 10.0) / 10.0;
            return true;
        }
        if (rect_hit(lo.gain_slider, x, y)) {
            double t = slider_ratio_hit(lo.gain_slider, x);
            double v = 0.1 + (20.0 - 0.1) * t;
            ctrl->gain = std::round(v * 10.0) / 10.0;
            return true;
        }
        return false;
    }

    if (spoof_mode) {
        if (rect_hit(lo.tx_slider, x, y)) {
            double t = slider_ratio_hit(lo.tx_slider, x);
            ctrl->tx_gain = std::round((100.0 * t) * 10.0) / 10.0;
            return true;
        }
        if (rect_hit(lo.gain_slider, x, y)) {
            double t = slider_ratio_hit(lo.gain_slider, x);
            double v = 0.1 + (20.0 - 0.1) * t;
            ctrl->gain = std::round(v * 10.0) / 10.0;
            return true;
        }
        if (rect_hit(lo.fs_slider, x, y)) {
            ctrl->fs_mhz = snap_fs_to_mode_grid_hz(0, ctrl->signal_mode) / 1e6;
            return true;
        }

        if (ctrl->show_detailed_ctrl) {
            if (rect_hit(lo.cn0_slider, x, y)) {
                double t = slider_ratio_hit(lo.cn0_slider, x);
                ctrl->target_cn0 = std::round((20.0 + (60.0 - 20.0) * t) * 2.0) / 2.0;
                return true;
            }
            if (rect_hit(lo.seed_slider, x, y)) {
                double t = slider_ratio_hit(lo.seed_slider, x);
                ctrl->seed = (unsigned)llround(1.0 + (65534.0) * t);
                return true;
            }
            if (rect_hit(lo.prn_slider, x, y)) {
                if (ctrl->sat_mode == 0) {
                    int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
                    double t = slider_ratio_hit(lo.prn_slider, x);
                    ctrl->single_prn = 1 + (int)llround(t * (max_prn - 1));
                    return true;
                }
            }
            if (rect_hit(lo.path_v_slider, x, y)) {
                double t = slider_ratio_hit(lo.path_v_slider, x);
                double min_v = 3.6, max_v = 2000.0;
                double v = min_v * std::exp(t * std::log(max_v / min_v));
                ctrl->path_vmax_kmh = std::round(v * 10.0) / 10.0;
                return true;
            }
            if (rect_hit(lo.path_a_slider, x, y)) {
                double t = slider_ratio_hit(lo.path_a_slider, x);
                double min_a = 0.2, max_a = 100.0;
                double a = min_a * std::exp(t * std::log(max_a / min_a));
                ctrl->path_accel_mps2 = std::round(a * 10.0) / 10.0;
                return true;
            }
            if (rect_hit(lo.ch_slider, x, y)) {
                double t = slider_ratio_hit(lo.ch_slider, x);
                ctrl->max_ch = clamp_int(1 + (int)llround(t * 15.0), 1, 16);
                return true;
            }

            int mode_segs = (ctrl->signal_mode == SIG_MODE_BDS) ? 3 : 2;
            int mode_idx = segmented_index_hit(lo.sw_mode, x, y, mode_segs);
            if (mode_idx >= 0) {
                if (ctrl->signal_mode == SIG_MODE_GPS) ctrl->sat_mode = (mode_idx == 0) ? 0 : 1;
                else if (ctrl->signal_mode == SIG_MODE_MIXED) ctrl->sat_mode = (mode_idx == 0) ? 1 : 2;
                else ctrl->sat_mode = mode_idx;

                if (ctrl->sat_mode == 0) {
                    int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
                    if (ctrl->single_prn < 1 || ctrl->single_prn > max_prn) {
                        ctrl->single_prn = 1;
                    }
                }
                return true;
            }

            if (rect_hit(lo.tg_meo, x, y)) { ctrl->meo_only = !ctrl->meo_only; return true; }
            if (rect_hit(lo.tg_iono, x, y)) { ctrl->iono_on = !ctrl->iono_on; return true; }
            if (rect_hit(lo.tg_clk, x, y)) { ctrl->usrp_external_clk = !ctrl->usrp_external_clk; return true; }
        }
        return false;
    }

    if (!ctrl->llh_ready && !mode_any) return false;

    if (rect_hit(lo.tx_slider, x, y)) {
        double t = slider_ratio_hit(lo.tx_slider, x);
        ctrl->tx_gain = std::round((100.0 * t) * 10.0) / 10.0;
        return true;
    } 
    if (rect_hit(lo.gain_slider, x, y)) {
        double t = slider_ratio_hit(lo.gain_slider, x);
        double v = 0.1 + (20.0 - 0.1) * t;
        ctrl->gain = std::round(v * 10.0) / 10.0;
        return true;
    } 
    if (rect_hit(lo.fs_slider, x, y)) {
        ctrl->fs_mhz = snap_fs_to_mode_grid_hz(0, ctrl->signal_mode) / 1e6;
        return true;
    } 

    if (ctrl->show_detailed_ctrl) {
        if (rect_hit(lo.cn0_slider, x, y)) {
            double t = slider_ratio_hit(lo.cn0_slider, x);
            ctrl->target_cn0 = std::round((20.0 + (60.0 - 20.0) * t) * 2.0) / 2.0;
            return true;
        } 
        if (rect_hit(lo.seed_slider, x, y)) {
            double t = slider_ratio_hit(lo.seed_slider, x);
            ctrl->seed = (unsigned)llround(1.0 + (65534.0) * t);
            return true;
        } 
        if (rect_hit(lo.prn_slider, x, y)) {
            if (ctrl->sat_mode == 0) {
                int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
                double t = slider_ratio_hit(lo.prn_slider, x);
                ctrl->single_prn = 1 + (int)llround(t * (max_prn - 1));
                return true;
            }
        } 
        if (rect_hit(lo.path_v_slider, x, y)) {
            double t = slider_ratio_hit(lo.path_v_slider, x);
            double min_v = 3.6, max_v = 2000.0;
            double v = min_v * std::exp(t * std::log(max_v / min_v)); 
            ctrl->path_vmax_kmh = std::round(v * 10.0) / 10.0;
            return true;
        } 
        if (rect_hit(lo.path_a_slider, x, y)) {
            double t = slider_ratio_hit(lo.path_a_slider, x);
            double min_a = 0.2, max_a = 100.0;
            double a = min_a * std::exp(t * std::log(max_a / min_a));
            ctrl->path_accel_mps2 = std::round(a * 10.0) / 10.0;
            return true;
        } 
        if (rect_hit(lo.ch_slider, x, y)) {
            double t = slider_ratio_hit(lo.ch_slider, x);
            ctrl->max_ch = clamp_int(1 + (int)llround(t * 15.0), 1, 16);
            return true;
        }

        if (spoof_mode) {
            int mode_segs = (ctrl->signal_mode == SIG_MODE_BDS) ? 3 : 2;
            int mode_idx = segmented_index_hit(lo.sw_mode, x, y, mode_segs);
            if (mode_idx >= 0) {
                if (ctrl->signal_mode == SIG_MODE_GPS) ctrl->sat_mode = (mode_idx == 0) ? 0 : 1;
                else if (ctrl->signal_mode == SIG_MODE_MIXED) ctrl->sat_mode = (mode_idx == 0) ? 1 : 2;
                else ctrl->sat_mode = mode_idx;

                if (ctrl->sat_mode == 0) {
                    int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
                    if (ctrl->single_prn < 1 || ctrl->single_prn > max_prn) {
                        ctrl->single_prn = 1;
                    }
                }
                return true;
            }

            if (rect_hit(lo.tg_meo, x, y)) { ctrl->meo_only = !ctrl->meo_only; return true; }
            if (rect_hit(lo.tg_iono, x, y)) { ctrl->iono_on = !ctrl->iono_on; return true; }
            if (rect_hit(lo.tg_clk, x, y)) { ctrl->usrp_external_clk = !ctrl->usrp_external_clk; return true; }
        }

    }

    return false;
}

bool control_logic_value_text_for_field(int field_id, char *out, size_t out_sz, GuiControlState *ctrl, std::mutex *ctrl_mtx) {
    if (!ctrl || !ctrl_mtx || !out || out_sz == 0) return false;
    if (field_id < 0 || field_id >= CTRL_SLIDER_COUNT) return false;
    std::lock_guard<std::mutex> lk(*ctrl_mtx);
    if (ctrl->running_ui) return false;

    bool spoof_mode = (ctrl->interference_selection == 0 && ctrl->spoof_allowed);
    bool jam_mode = (ctrl->interference_selection == 1);
    if (!ctrl->llh_ready && !spoof_mode && !jam_mode) return false;
    if (jam_mode && field_id != CTRL_SLIDER_TX && field_id != CTRL_SLIDER_GAIN &&
        field_id != CTRL_SLIDER_CN0) return false;

    switch (field_id) {
    case CTRL_SLIDER_TX: std::snprintf(out, out_sz, "%.1f", ctrl->tx_gain); return true;
    case CTRL_SLIDER_GAIN: std::snprintf(out, out_sz, "%.2f", ctrl->gain); return true;
    case CTRL_SLIDER_FS: std::snprintf(out, out_sz, "%.1f", ctrl->fs_mhz); return true;
    case CTRL_SLIDER_CN0: std::snprintf(out, out_sz, "%.1f", ctrl->target_cn0); return true;
    case CTRL_SLIDER_SEED: std::snprintf(out, out_sz, "%u", ctrl->seed); return true;
    case CTRL_SLIDER_PRN: 
        if (ctrl->sat_mode != 0) return false;
        std::snprintf(out, out_sz, "%d", ctrl->single_prn); return true;
    case CTRL_SLIDER_PATH_V: std::snprintf(out, out_sz, "%.1f", ctrl->path_vmax_kmh); return true;
    case CTRL_SLIDER_PATH_A: std::snprintf(out, out_sz, "%.1f", ctrl->path_accel_mps2); return true;
    case CTRL_SLIDER_CH: std::snprintf(out, out_sz, "%d", ctrl->max_ch); return true;
    default: return false;
    }
}

bool control_logic_handle_value_input(int field_id, const char *input, GuiControlState *ctrl, std::mutex *ctrl_mtx) {
    if (!ctrl || !ctrl_mtx || !input) return false;
    if (field_id < 0 || field_id >= CTRL_SLIDER_COUNT) return false;

    char *endp = nullptr;
    double x = std::strtod(input, &endp);
    if (endp == input || *endp != '\0') return false;

    std::lock_guard<std::mutex> lk(*ctrl_mtx);
    if (ctrl->running_ui) return false;
    bool spoof_mode = (ctrl->interference_selection == 0 && ctrl->spoof_allowed);
    bool jam_mode = (ctrl->interference_selection == 1);
    if (!ctrl->llh_ready && !spoof_mode && !jam_mode) return false;
    if (jam_mode && field_id != CTRL_SLIDER_TX && field_id != CTRL_SLIDER_GAIN &&
        field_id != CTRL_SLIDER_CN0) return false;

    switch (field_id) {
    case CTRL_SLIDER_TX: ctrl->tx_gain = clamp_double(std::round(x * 10.0) / 10.0, 0.0, 100.0); return true;
    case CTRL_SLIDER_GAIN: ctrl->gain = clamp_double(std::round(x * 10.0) / 10.0, 0.1, 20.0); return true;
    case CTRL_SLIDER_FS: {
        double min_fs = mode_min_fs_hz(ctrl->signal_mode) / 1e6;
        if (x < min_fs) x = min_fs;
        if (x > 31.2) x = 31.2;
        ctrl->fs_mhz = x;
        return true;
    }
    case CTRL_SLIDER_CN0: ctrl->target_cn0 = clamp_double(std::round(x * 2.0) / 2.0, 20.0, 60.0); return true;
    case CTRL_SLIDER_SEED: {
        long long v = llround(x);
        if (v < 1) v = 1; 
        if (v > 65535LL) v = 65535LL; // 解決 Warning：分行寫
        ctrl->seed = (unsigned)v; return true;
    }
    case CTRL_SLIDER_PRN: {
        if (ctrl->sat_mode == 0) {
            int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
            int want = (int)llround(x);
            ctrl->single_prn = clamp_int(want, 1, max_prn);
            return true;
        }
        return false;
    }
    case CTRL_SLIDER_PATH_V: ctrl->path_vmax_kmh = clamp_double(std::round(x * 10.0) / 10.0, 3.6, 2000.0); return true;
    case CTRL_SLIDER_PATH_A: ctrl->path_accel_mps2 = clamp_double(std::round(x * 10.0) / 10.0, 0.2, 100.0); return true;
    case CTRL_SLIDER_CH: ctrl->max_ch = clamp_int((int)llround(x), 1, 16); return true;
    default: return false;
    }
}

bool control_logic_handle_slider_drag(int slider_id, int x, int win_width, int win_height, GuiControlState *ctrl, std::mutex *ctrl_mtx) {
    if (!ctrl || !ctrl_mtx) return false;
    if (slider_id < 0 || slider_id >= CTRL_SLIDER_COUNT) return false;

    bool detailed = false;
    {
        std::lock_guard<std::mutex> lk(*ctrl_mtx);
        detailed = ctrl->show_detailed_ctrl;
    }

    ControlLayout lo; compute_control_layout(win_width, win_height, &lo, detailed);

    Rect r = lo.tx_slider;
    switch (slider_id) {
    case CTRL_SLIDER_TX: r = lo.tx_slider; break;
    case CTRL_SLIDER_GAIN: r = lo.gain_slider; break;
    case CTRL_SLIDER_FS: r = lo.fs_slider; break;
    case CTRL_SLIDER_CN0: r = lo.cn0_slider; break;
    case CTRL_SLIDER_SEED: r = lo.seed_slider; break;
    case CTRL_SLIDER_PRN: r = lo.prn_slider; break;
    case CTRL_SLIDER_PATH_V: r = lo.path_v_slider; break;
    case CTRL_SLIDER_PATH_A: r = lo.path_a_slider; break;
    case CTRL_SLIDER_CH: r = lo.ch_slider; break;
    default: return false;
    }

    std::lock_guard<std::mutex> lk(*ctrl_mtx);
    if (ctrl->running_ui) return false;
    bool spoof_mode = (ctrl->interference_selection == 0 && ctrl->spoof_allowed);
    bool jam_mode = (ctrl->interference_selection == 1);
    if (!ctrl->llh_ready && !spoof_mode && !jam_mode) return false;
    if (jam_mode && slider_id != CTRL_SLIDER_TX && slider_id != CTRL_SLIDER_GAIN &&
        slider_id != CTRL_SLIDER_CN0) return false;

    double t = slider_ratio_hit(r, x);
    bool changed = false;

    switch (slider_id) {
    case CTRL_SLIDER_TX: {
        double v = std::round((100.0 * t) * 10.0) / 10.0;
        if (std::fabs(ctrl->tx_gain - v) > 1e-9) { ctrl->tx_gain = v; changed = true; } break;
    }
    case CTRL_SLIDER_GAIN: {
        double v = 0.1 + (20.0 - 0.1) * t; v = std::round(v * 10.0) / 10.0;
        if (std::fabs(ctrl->gain - v) > 1e-9) { ctrl->gain = v; changed = true; } break;
    }
    case CTRL_SLIDER_FS: {
        double v = 2.6 + (31.2 - 2.6) * t; 
        double min_fs = mode_min_fs_hz(ctrl->signal_mode) / 1e6;
        if (v < min_fs) v = min_fs; 
        if (std::fabs(ctrl->fs_mhz - v) > 1e-9) { ctrl->fs_mhz = v; changed = true; } break;
    }
    case CTRL_SLIDER_CN0: {
        double v = 20.0 + (60.0 - 20.0) * t; v = std::round(v * 2.0) / 2.0;
        if (std::fabs(ctrl->target_cn0 - v) > 1e-9) { ctrl->target_cn0 = v; changed = true; } break;
    }
    case CTRL_SLIDER_SEED: {
        double v = 1.0 + 65534.0 * t;
        if (v < 1.0) v = 1.0; 
        if (v > 65535.0) v = 65535.0; // 解決 Warning：分行寫
        unsigned s = (unsigned)llround(v);
        if (ctrl->seed != s) { ctrl->seed = s; changed = true; } break;
    }
    case CTRL_SLIDER_PRN: {
        if (ctrl->sat_mode == 0) {
            int max_prn = (ctrl->signal_mode == SIG_MODE_GPS) ? 32 : 63;
            int prn = 1 + (int)llround(t * (max_prn - 1));
            if (ctrl->single_prn != prn) {
                ctrl->single_prn = prn; changed = true;
            }
        } break;
    }
    case CTRL_SLIDER_PATH_V: {
        double min_v = 3.6, max_v = 2000.0;
        double v = min_v * std::exp(t * std::log(max_v / min_v)); 
        v = std::round(v * 10.0) / 10.0;
        if (std::fabs(ctrl->path_vmax_kmh - v) > 1e-9) { ctrl->path_vmax_kmh = v; changed = true; } break;
    }
    case CTRL_SLIDER_PATH_A: {
        double min_a = 0.2, max_a = 100.0;
        double a = min_a * std::exp(t * std::log(max_a / min_a)); 
        a = std::round(a * 10.0) / 10.0;
        if (std::fabs(ctrl->path_accel_mps2 - a) > 1e-9) { ctrl->path_accel_mps2 = a; changed = true; } break;
    }
    case CTRL_SLIDER_CH: {
        int ch = 1 + (int)llround(t * 15.0); ch = clamp_int(ch, 1, 16);
        if (ctrl->max_ch != ch) { ctrl->max_ch = ch; changed = true; } break;
    }
    default: break;
    }
    return changed;
}