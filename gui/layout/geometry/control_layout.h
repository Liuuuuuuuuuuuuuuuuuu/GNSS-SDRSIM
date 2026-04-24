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
    Rect header_gear;
    Rect header_title;
    Rect header_utc;
    Rect header_bdt;
    Rect header_gpst;
    Rect detail_sats;
    Rect content_frame;
    Rect btn_tab_simple;
    Rect btn_tab_detail;
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
    Rect btn_return;
    Rect btn_exit;
};

enum ControlLayoutElementId {
    CTRL_LAYOUT_ELEMENT_NONE = -1,
    CTRL_LAYOUT_ELEMENT_HEADER_GEAR = 0,
    CTRL_LAYOUT_ELEMENT_HEADER_TITLE,
    CTRL_LAYOUT_ELEMENT_HEADER_UTC,
    CTRL_LAYOUT_ELEMENT_HEADER_BDT,
    CTRL_LAYOUT_ELEMENT_HEADER_GPST,
    CTRL_LAYOUT_ELEMENT_DETAIL_SATS,
    CTRL_LAYOUT_ELEMENT_BTN_TAB_SIMPLE,
    CTRL_LAYOUT_ELEMENT_BTN_TAB_DETAIL,
    CTRL_LAYOUT_ELEMENT_TX_SLIDER,
    CTRL_LAYOUT_ELEMENT_GAIN_SLIDER,
    CTRL_LAYOUT_ELEMENT_FS_SLIDER,
    CTRL_LAYOUT_ELEMENT_CN0_SLIDER,
    CTRL_LAYOUT_ELEMENT_HEIGHT_SLIDER,
    CTRL_LAYOUT_ELEMENT_PRN_SLIDER,
    CTRL_LAYOUT_ELEMENT_PATH_V_SLIDER,
    CTRL_LAYOUT_ELEMENT_PATH_A_SLIDER,
    CTRL_LAYOUT_ELEMENT_CH_SLIDER,
    CTRL_LAYOUT_ELEMENT_SW_MODE,
    CTRL_LAYOUT_ELEMENT_SW_SYS,
    CTRL_LAYOUT_ELEMENT_TG_MEO,
    CTRL_LAYOUT_ELEMENT_TG_IONO,
    CTRL_LAYOUT_ELEMENT_TG_CLK,
    CTRL_LAYOUT_ELEMENT_SW_FMT,
    CTRL_LAYOUT_ELEMENT_SW_JAM,
    CTRL_LAYOUT_ELEMENT_BTN_START,
    CTRL_LAYOUT_ELEMENT_BTN_STOP,
    CTRL_LAYOUT_ELEMENT_BTN_RETURN,
    CTRL_LAYOUT_ELEMENT_BTN_EXIT,
    CTRL_LAYOUT_ELEMENT_COUNT
};

struct ControlRectAdjustment {
    int dx = 0;
    int dy = 0;
    int dw = 0;
    int dh = 0;
};

struct ControlLayoutOverrides {
    ControlRectAdjustment entries[CTRL_LAYOUT_ELEMENT_COUNT];
};

struct ControlSliderPartOverrides {
    ControlRectAdjustment label[CTRL_LAYOUT_ELEMENT_COUNT];
    ControlRectAdjustment track[CTRL_LAYOUT_ELEMENT_COUNT];
    ControlRectAdjustment value[CTRL_LAYOUT_ELEMENT_COUNT];
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

void compute_control_layout(int win_width, int win_height, ControlLayout *lo, bool detailed,
                            bool single_system_sat_layout = false,
                            const ControlLayoutOverrides *overrides = nullptr);
int control_slider_hit_test(int x, int y, int win_width, int win_height, bool detailed,
                            bool single_system_sat_layout = false);
int control_value_hit_test(int x, int y, int win_width, int win_height, bool detailed,
                           bool single_system_sat_layout = false);
Rect *control_layout_mutable_rect(ControlLayout *lo, ControlLayoutElementId id);
const Rect *control_layout_rect(const ControlLayout *lo, ControlLayoutElementId id);
ControlLayoutElementId control_layout_hit_test(const ControlLayout &lo, int x, int y);
const char *control_layout_element_debug_name(ControlLayoutElementId id);

#endif