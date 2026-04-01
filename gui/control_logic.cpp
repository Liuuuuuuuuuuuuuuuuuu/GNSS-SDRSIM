#include "gui/control_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include "bdssim.h"
#include "globals.h"
}

static inline double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline double mode_min_fs_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_MIN_FS_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_MIN_FS_HZ;
    return RF_BDS_ONLY_MIN_FS_HZ;
}

static inline double mode_default_fs_hz(uint8_t signal_mode)
{
    if (signal_mode == SIG_MODE_GPS) return RF_GPS_ONLY_MIN_FS_HZ;
    if (signal_mode == SIG_MODE_MIXED) return RF_MIXED_MIN_FS_HZ;
    return RF_BDS_ONLY_MIN_FS_HZ;
}

static inline double snap_fs_to_mode_grid_hz(double fs_hz, uint8_t signal_mode)
{
    const double step = RF_FS_STEP_HZ;
    const double min_fs = mode_min_fs_hz(signal_mode);
    if (fs_hz < min_fs) fs_hz = min_fs;

    double k = std::round(fs_hz / step);
    if (k < 1.0) k = 1.0;
    fs_hz = k * step;

    if (fs_hz < min_fs) fs_hz = min_fs;
    return fs_hz;
}

bool control_logic_handle_click(int x,
                                int y,
                                int win_width,
                                int win_height,
                                GuiControlState *ctrl,
                                std::mutex *ctrl_mtx,
                                std::atomic<uint32_t> *start_req,
                                std::atomic<uint32_t> *stop_req,
                                std::atomic<uint32_t> *exit_req,
                                volatile int *runtime_abort)
{
    if (!ctrl || !ctrl_mtx || !start_req || !stop_req || !exit_req || !runtime_abort) return false;
    if (x < 0 || x >= win_width / 2) return false;

    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo);
    if (!rect_hit(lo.panel, x, y)) return false;

    bool changed = false;
    std::lock_guard<std::mutex> lk(*ctrl_mtx);

    if (rect_hit(lo.btn_exit, x, y)) {
        if (!ctrl->running_ui) {
            exit_req->fetch_add(1);
            changed = true;
        }
        return changed;
    }

    if (rect_hit(lo.btn_start, x, y)) {
        if (!ctrl->running_ui && (ctrl->llh_ready || ctrl->interference_mode)) {
            ctrl->running_ui = true;
            start_req->fetch_add(1);
            changed = true;
        }
        return changed;
    }
    if (rect_hit(lo.btn_stop, x, y)) {
        if (ctrl->running_ui) {
            ctrl->running_ui = false;
            *runtime_abort = 1;
            stop_req->fetch_add(1);
            changed = true;
        }
        return changed;
    }

    if (ctrl->running_ui) return false;

    int sys_idx = segmented_index_hit(lo.sw_sys, x, y, 3);
    if (sys_idx >= 0) {
        uint8_t next_mode = (sys_idx == 0) ? SIG_MODE_BDS : ((sys_idx == 1) ? SIG_MODE_MIXED : SIG_MODE_GPS);
        if (ctrl->signal_mode != next_mode) {
            ctrl->signal_mode = next_mode;
            ctrl->fs_mhz = mode_default_fs_hz(ctrl->signal_mode) / 1e6;
            changed = true;
        }
        return changed;
    }

    if (!ctrl->llh_ready) return false;

    if (rect_hit(lo.tx_slider, x, y)) {
        double t = slider_ratio_hit(lo.tx_slider, x);
        ctrl->tx_gain = std::round((100.0 * t) * 10.0) / 10.0;
        changed = true;
    } else if (rect_hit(lo.gain_slider, x, y)) {
        double t = slider_ratio_hit(lo.gain_slider, x);
        double v = 0.1 + (20.0 - 0.1) * t;
        ctrl->gain = std::round(v * 10.0) / 10.0;
        changed = true;
    } else if (rect_hit(lo.fs_slider, x, y)) {
        double t = slider_ratio_hit(lo.fs_slider, x);
        double fs_min = mode_min_fs_hz(ctrl->signal_mode);
        double fs_max = 31.2e6;
        double fs = fs_min + (fs_max - fs_min) * t;
        fs = snap_fs_to_mode_grid_hz(fs, ctrl->signal_mode);
        if (fs > fs_max) fs = fs_max;
        ctrl->fs_mhz = fs / 1e6;
        changed = true;
    } else if (rect_hit(lo.cn0_slider, x, y)) {
        double t = slider_ratio_hit(lo.cn0_slider, x);
        double v = 20.0 + (60.0 - 20.0) * t;
        ctrl->target_cn0 = std::round(v * 2.0) / 2.0;
        changed = true;
    } else if (rect_hit(lo.seed_slider, x, y)) {
        double t = slider_ratio_hit(lo.seed_slider, x);
        double v = 1.0 + (999999999.0 - 1.0) * t;
        if (v < 1.0) v = 1.0;
        if (v > 999999999.0) v = 999999999.0;
        ctrl->seed = (unsigned)llround(v);
        changed = true;
    } else if (rect_hit(lo.prn_slider, x, y)) {
        if (ctrl->sat_mode == 0 && ctrl->single_candidate_count > 0) {
            double t = slider_ratio_hit(lo.prn_slider, x);
            int idx = (int)llround(t * (double)(ctrl->single_candidate_count - 1));
            ctrl->single_candidate_idx = clamp_int(idx, 0, ctrl->single_candidate_count - 1);
            ctrl->single_prn = ctrl->single_candidates[ctrl->single_candidate_idx];
            changed = true;
        }
    } else if (rect_hit(lo.path_v_slider, x, y)) {
        double t = slider_ratio_hit(lo.path_v_slider, x);
        double v = 3.6 + (288.0 - 3.6) * t;
        ctrl->path_vmax_kmh = std::round(v * 10.0) / 10.0;
        changed = true;
    } else if (rect_hit(lo.path_a_slider, x, y)) {
        double t = slider_ratio_hit(lo.path_a_slider, x);
        double v = 0.2 + (10.0 - 0.2) * t;
        ctrl->path_accel_mps2 = std::round(v * 10.0) / 10.0;
        changed = true;
    } else if (rect_hit(lo.ch_slider, x, y)) {
        double t = slider_ratio_hit(lo.ch_slider, x);
        int ch = 1 + (int)llround(t * 15.0);
        ctrl->max_ch = clamp_int(ch, 1, 16);
        changed = true;
    } else {
        int mode_idx = segmented_index_hit(lo.sw_mode, x, y, 3);
        if (mode_idx >= 0) {
            ctrl->sat_mode = mode_idx;
            if (ctrl->sat_mode == 0) {
                if (ctrl->single_candidate_count > 0) {
                    ctrl->single_candidate_idx =
                        clamp_int(ctrl->single_candidate_idx, 0, ctrl->single_candidate_count - 1);
                    ctrl->single_prn = ctrl->single_candidates[ctrl->single_candidate_idx];
                } else {
                    ctrl->single_prn = 0;
                }
            }
            changed = true;
        } else if (rect_hit(lo.tg_meo, x, y)) {
            ctrl->meo_only = !ctrl->meo_only;
            changed = true;
        } else if (rect_hit(lo.tg_iono, x, y)) {
            ctrl->iono_on = !ctrl->iono_on;
            changed = true;
        } else if (rect_hit(lo.tg_clk, x, y)) {
            ctrl->usrp_external_clk = !ctrl->usrp_external_clk;
            changed = true;
        } else {
            int fmt_idx = segmented_index_hit(lo.sw_fmt, x, y, 2);
            if (fmt_idx >= 0) {
                ctrl->byte_output = (fmt_idx == 1);
                changed = true;
            } else {
                int jam_idx = segmented_index_hit(lo.sw_jam, x, y, 2);
                if (jam_idx >= 0) {
                    ctrl->interference_mode = (jam_idx == 1);
                    if (ctrl->interference_mode && ctrl->signal_mode == SIG_MODE_GPS) {
                        ctrl->fs_mhz = RF_GPS_ONLY_MIN_FS_HZ / 1e6;
                    }
                    changed = true;
                }
            }
        }
    }

    return changed;
}

bool control_logic_value_text_for_field(int field_id,
                                        char *out,
                                        size_t out_sz,
                                        GuiControlState *ctrl,
                                        std::mutex *ctrl_mtx)
{
    if (!ctrl || !ctrl_mtx || !out || out_sz == 0) return false;
    if (field_id < 0 || field_id >= CTRL_SLIDER_COUNT) return false;

    std::lock_guard<std::mutex> lk(*ctrl_mtx);
    if (ctrl->running_ui || !ctrl->llh_ready) return false;

    switch (field_id) {
    case CTRL_SLIDER_TX:
        std::snprintf(out, out_sz, "%.1f", ctrl->tx_gain);
        return true;
    case CTRL_SLIDER_GAIN:
        std::snprintf(out, out_sz, "%.2f", ctrl->gain);
        return true;
    case CTRL_SLIDER_FS:
        std::snprintf(out, out_sz, "%.2f", ctrl->fs_mhz);
        return true;
    case CTRL_SLIDER_CN0:
        std::snprintf(out, out_sz, "%.1f", ctrl->target_cn0);
        return true;
    case CTRL_SLIDER_SEED:
        std::snprintf(out, out_sz, "%u", ctrl->seed);
        return true;
    case CTRL_SLIDER_PRN:
        if (!(ctrl->sat_mode == 0 && ctrl->single_candidate_count > 0)) return false;
        std::snprintf(out, out_sz, "%d", ctrl->single_prn);
        return true;
    case CTRL_SLIDER_PATH_V:
        std::snprintf(out, out_sz, "%.1f", ctrl->path_vmax_kmh);
        return true;
    case CTRL_SLIDER_PATH_A:
        std::snprintf(out, out_sz, "%.1f", ctrl->path_accel_mps2);
        return true;
    case CTRL_SLIDER_CH:
        std::snprintf(out, out_sz, "%d", ctrl->max_ch);
        return true;
    default:
        return false;
    }
}

bool control_logic_handle_value_input(int field_id,
                                      const char *input,
                                      GuiControlState *ctrl,
                                      std::mutex *ctrl_mtx)
{
    if (!ctrl || !ctrl_mtx || !input) return false;
    if (field_id < 0 || field_id >= CTRL_SLIDER_COUNT) return false;

    char *endp = nullptr;
    double x = std::strtod(input, &endp);
    if (endp == input || *endp != '\0') return false;

    std::lock_guard<std::mutex> lk(*ctrl_mtx);
    if (ctrl->running_ui || !ctrl->llh_ready) return false;

    switch (field_id) {
    case CTRL_SLIDER_TX:
        ctrl->tx_gain = clamp_double(std::round(x * 10.0) / 10.0, 0.0, 100.0);
        return true;
    case CTRL_SLIDER_GAIN:
        ctrl->gain = clamp_double(std::round(x * 10.0) / 10.0, 0.1, 20.0);
        return true;
    case CTRL_SLIDER_FS: {
        double fs_hz = snap_fs_to_mode_grid_hz(x * 1e6, ctrl->signal_mode);
        if (fs_hz > 31.2e6) fs_hz = 31.2e6;
        if (fs_hz < mode_min_fs_hz(ctrl->signal_mode)) fs_hz = mode_min_fs_hz(ctrl->signal_mode);
        ctrl->fs_mhz = fs_hz / 1e6;
        return true;
    }
    case CTRL_SLIDER_CN0:
        ctrl->target_cn0 = clamp_double(std::round(x * 2.0) / 2.0, 20.0, 60.0);
        return true;
    case CTRL_SLIDER_SEED: {
        long long v = llround(x);
        if (v < 1) v = 1;
        if (v > 999999999LL) v = 999999999LL;
        ctrl->seed = (unsigned)v;
        return true;
    }
    case CTRL_SLIDER_PRN: {
        if (!(ctrl->sat_mode == 0 && ctrl->single_candidate_count > 0)) return false;
        int want = (int)llround(x);
        int best_idx = 0;
        int best_d = 1 << 30;
        for (int i = 0; i < ctrl->single_candidate_count; ++i) {
            int d = std::abs(ctrl->single_candidates[i] - want);
            if (d < best_d) {
                best_d = d;
                best_idx = i;
            }
        }
        ctrl->single_candidate_idx = best_idx;
        ctrl->single_prn = ctrl->single_candidates[best_idx];
        return true;
    }
    case CTRL_SLIDER_PATH_V:
        ctrl->path_vmax_kmh = clamp_double(std::round(x * 10.0) / 10.0, 3.6, 288.0);
        return true;
    case CTRL_SLIDER_PATH_A:
        ctrl->path_accel_mps2 = clamp_double(std::round(x * 10.0) / 10.0, 0.2, 10.0);
        return true;
    case CTRL_SLIDER_CH:
        ctrl->max_ch = clamp_int((int)llround(x), 1, 16);
        return true;
    default:
        return false;
    }
}

bool control_logic_handle_slider_drag(int slider_id,
                                      int x,
                                      int win_width,
                                      int win_height,
                                      GuiControlState *ctrl,
                                      std::mutex *ctrl_mtx)
{
    if (!ctrl || !ctrl_mtx) return false;
    if (slider_id < 0 || slider_id >= CTRL_SLIDER_COUNT) return false;

    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo);

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
    if (ctrl->running_ui || !ctrl->llh_ready) return false;

    double t = slider_ratio_hit(r, x);
    bool changed = false;

    switch (slider_id) {
    case CTRL_SLIDER_TX: {
        double v = std::round((100.0 * t) * 10.0) / 10.0;
        if (std::fabs(ctrl->tx_gain - v) > 1e-9) {
            ctrl->tx_gain = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_GAIN: {
        double v = 0.1 + (20.0 - 0.1) * t;
        v = std::round(v * 10.0) / 10.0;
        if (std::fabs(ctrl->gain - v) > 1e-9) {
            ctrl->gain = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_FS: {
        double fs_min = mode_min_fs_hz(ctrl->signal_mode);
        double fs_max = 31.2e6;
        double fs = fs_min + (fs_max - fs_min) * t;
        fs = snap_fs_to_mode_grid_hz(fs, ctrl->signal_mode);
        if (fs > fs_max) fs = fs_max;
        double v = fs / 1e6;
        if (std::fabs(ctrl->fs_mhz - v) > 1e-9) {
            ctrl->fs_mhz = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_CN0: {
        double v = 20.0 + (60.0 - 20.0) * t;
        v = std::round(v * 2.0) / 2.0;
        if (std::fabs(ctrl->target_cn0 - v) > 1e-9) {
            ctrl->target_cn0 = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_SEED: {
        double v = 1.0 + (999999999.0 - 1.0) * t;
        if (v < 1.0) v = 1.0;
        if (v > 999999999.0) v = 999999999.0;
        unsigned s = (unsigned)llround(v);
        if (ctrl->seed != s) {
            ctrl->seed = s;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_PRN: {
        if (ctrl->sat_mode == 0 && ctrl->single_candidate_count > 0) {
            int idx = (int)llround(t * (double)(ctrl->single_candidate_count - 1));
            idx = clamp_int(idx, 0, ctrl->single_candidate_count - 1);
            if (ctrl->single_candidate_idx != idx) {
                ctrl->single_candidate_idx = idx;
                ctrl->single_prn = ctrl->single_candidates[idx];
                changed = true;
            }
        }
        break;
    }
    case CTRL_SLIDER_PATH_V: {
        double v = 3.6 + (288.0 - 3.6) * t;
        v = std::round(v * 10.0) / 10.0;
        if (std::fabs(ctrl->path_vmax_kmh - v) > 1e-9) {
            ctrl->path_vmax_kmh = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_PATH_A: {
        double v = 0.2 + (10.0 - 0.2) * t;
        v = std::round(v * 10.0) / 10.0;
        if (std::fabs(ctrl->path_accel_mps2 - v) > 1e-9) {
            ctrl->path_accel_mps2 = v;
            changed = true;
        }
        break;
    }
    case CTRL_SLIDER_CH: {
        int ch = 1 + (int)llround(t * 15.0);
        ch = clamp_int(ch, 1, 16);
        if (ctrl->max_ch != ch) {
            ctrl->max_ch = ch;
            changed = true;
        }
        break;
    }
    default:
        break;
    }

    return changed;
}
