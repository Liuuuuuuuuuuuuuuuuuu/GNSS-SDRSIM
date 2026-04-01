#include "gui/control_layout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static inline double clamp_double(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool rect_hit(const Rect &r, int x, int y)
{
    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

int segmented_index_hit(const Rect &r, int x, int y, int segments)
{
    if (segments <= 0) return -1;
    if (!rect_hit(r, x, y)) return -1;

    int seg_w = r.w / segments;
    if (seg_w <= 0) return -1;
    int idx = (x - r.x) / seg_w;
    if (idx < 0) idx = 0;
    if (idx >= segments) idx = segments - 1;
    return idx;
}

double slider_ratio_hit(const Rect &r, int x)
{
    if (r.w <= 1) return 0.0;
    double t = (double)(x - r.x) / (double)(r.w - 1);
    return clamp_double(t, 0.0, 1.0);
}

Rect slider_value_rect(const Rect &r)
{
    int w = 78;
    if (w > r.w - 24) w = std::max(44, r.w / 2);
    return {r.x + r.w - w, r.y - 13, w, 12};
}

void compute_control_layout(int win_width, int win_height, ControlLayout *lo)
{
    std::memset(lo, 0, sizeof(*lo));

    int half_w = win_width / 2;
    lo->panel.x = 0;
    lo->panel.y = (win_height * 3) / 4;
    lo->panel.w = half_w;
    lo->panel.h = win_height - lo->panel.y;

    int pad = 12;
    int row_h = 16;
    int row_gap = 16;
    int col_gap = 8;
    int col_w = (lo->panel.w - 2 * pad - 2 * col_gap) / 3;
    if (col_w < 120) col_w = 120;

    int base_y = lo->panel.y + 46;
    int x_l = lo->panel.x + pad;
    int x_m = x_l + col_w + col_gap;
    int x_r = x_m + col_w + col_gap;

    auto set_slider = [&](Rect *slider_r, int x, int y) {
        slider_r->x = x;
        slider_r->y = y;
        slider_r->w = col_w;
        slider_r->h = row_h;
    };

    set_slider(&lo->tx_slider, x_l, base_y + 0 * (row_h + row_gap));
    set_slider(&lo->gain_slider, x_m, base_y + 0 * (row_h + row_gap));
    set_slider(&lo->fs_slider, x_r, base_y + 0 * (row_h + row_gap));

    set_slider(&lo->cn0_slider, x_l, base_y + 1 * (row_h + row_gap));
    set_slider(&lo->seed_slider, x_m, base_y + 1 * (row_h + row_gap));
    set_slider(&lo->prn_slider, x_r, base_y + 1 * (row_h + row_gap));
    set_slider(&lo->path_v_slider, x_l, base_y + 2 * (row_h + row_gap));
    set_slider(&lo->path_a_slider, x_m, base_y + 2 * (row_h + row_gap));
    set_slider(&lo->ch_slider, x_r, base_y + 2 * (row_h + row_gap));

    int gap_params_to_mode = 0;
    int gap_mode_to_toggle = 14;
    int gap_toggle_to_action = 10;

    int mode_y = base_y + 3 * (row_h + row_gap) + gap_params_to_mode;
    int sw_gap = 8;
    int sw_w = (lo->panel.w - 2 * pad - sw_gap) / 2;
    if (sw_w < 120) sw_w = 120;
    lo->sw_mode = {x_l, mode_y, sw_w, row_h};
    lo->sw_sys = {x_l + sw_w + sw_gap, mode_y, sw_w, row_h};

    int tg_y = mode_y + row_h + gap_mode_to_toggle;
    int tg_w = (lo->panel.w - 2 * pad - 4 * 6) / 5;
    if (tg_w < 60) tg_w = 60;
    lo->tg_meo = {x_l, tg_y, tg_w, row_h};
    lo->tg_iono = {x_l + 1 * (tg_w + 6), tg_y, tg_w, row_h};
    lo->tg_clk = {x_l + 2 * (tg_w + 6), tg_y, tg_w, row_h};
    lo->sw_fmt = {x_l + 3 * (tg_w + 6), tg_y, tg_w, row_h};
    lo->sw_jam = {x_l + 4 * (tg_w + 6), tg_y, tg_w, row_h};

    int action_y = tg_y + row_h + gap_toggle_to_action;
    int action_w = (lo->panel.w - 2 * pad - 8) / 2;
    if (action_w < 120) action_w = 120;
    lo->btn_start = {x_l, action_y, action_w, 20};
    lo->btn_stop = {x_l + action_w + 8, action_y, action_w, 20};

    int exit_size = 14;
    int exit_margin_right = 10;
    int exit_margin_bottom = 4;
    lo->btn_exit = {
        lo->panel.x + lo->panel.w - exit_margin_right - exit_size,
        lo->panel.y + lo->panel.h - exit_margin_bottom - exit_size,
        exit_size,
        exit_size
    };
}

int control_slider_hit_test(int x, int y, int win_width, int win_height)
{
    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo);

    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(lo.tx_slider, x, y) && !rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(lo.gain_slider, x, y) && !rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(lo.fs_slider, x, y) && !rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (rect_hit(lo.cn0_slider, x, y) && !rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (rect_hit(lo.seed_slider, x, y) && !rect_hit(slider_value_rect(lo.seed_slider), x, y)) return CTRL_SLIDER_SEED;
    if (rect_hit(lo.prn_slider, x, y) && !rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (rect_hit(lo.path_v_slider, x, y) && !rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (rect_hit(lo.path_a_slider, x, y) && !rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (rect_hit(lo.ch_slider, x, y) && !rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}

int control_value_hit_test(int x, int y, int win_width, int win_height)
{
    ControlLayout lo;
    compute_control_layout(win_width, win_height, &lo);

    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (rect_hit(slider_value_rect(lo.seed_slider), x, y)) return CTRL_SLIDER_SEED;
    if (rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}
