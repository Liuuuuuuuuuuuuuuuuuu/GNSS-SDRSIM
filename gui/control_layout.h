#ifndef CONTROL_LAYOUT_H
#define CONTROL_LAYOUT_H

#include <cstddef>

struct Rect {
    int x;
    int y;
    int w;
    int h;
};

struct ControlLayout {
    Rect panel;
    Rect tx_slider;
    Rect gain_slider;
    Rect fs_slider;
    Rect cn0_slider;
    Rect seed_slider;
    Rect prn_slider;
    Rect path_v_slider;
    Rect path_a_slider;
    Rect ch_slider;
    Rect sw_mode;
    Rect sw_sys;
    Rect tg_meo;
    Rect tg_iono;
    Rect tg_clk;
    Rect sw_fmt;
    Rect sw_jam;
    Rect btn_start;
    Rect btn_stop;
    Rect btn_exit;
};

enum {
    CTRL_SLIDER_TX = 0,
    CTRL_SLIDER_GAIN,
    CTRL_SLIDER_FS,
    CTRL_SLIDER_CN0,
    CTRL_SLIDER_SEED,
    CTRL_SLIDER_PRN,
    CTRL_SLIDER_PATH_V,
    CTRL_SLIDER_PATH_A,
    CTRL_SLIDER_CH,
    CTRL_SLIDER_COUNT
};

bool rect_hit(const Rect &r, int x, int y);
int segmented_index_hit(const Rect &r, int x, int y, int segments);
double slider_ratio_hit(const Rect &r, int x);
Rect slider_value_rect(const Rect &r);

void compute_control_layout(int win_width, int win_height, ControlLayout *lo);
int control_slider_hit_test(int x, int y, int win_width, int win_height);
int control_value_hit_test(int x, int y, int win_width, int win_height);

#endif
