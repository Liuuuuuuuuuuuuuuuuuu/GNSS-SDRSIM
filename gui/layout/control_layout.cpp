#include "gui/layout/control_layout.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

bool rect_hit(const Rect &r, int x, int y) {
    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

int segmented_index_hit(const Rect &r, int x, int y, int segments) {
    if (segments <= 0) return -1;
    if (!rect_hit(r, x, y)) return -1;
    int seg_w = r.w / segments;
    if (seg_w <= 0) return -1;
    int idx = (x - r.x) / seg_w;
    if (idx < 0) idx = 0;
    if (idx >= segments) idx = segments - 1;
    return idx;
}

double slider_ratio_hit(const Rect &r, int x) {
    if (r.w <= 1) return 0.0;
    double t = (double)(x - r.x) / (double)(r.w - 1);
    return clamp_double(t, 0.0, 1.0);
}

// 根據模式動態調整數值框的大小與推升高度
Rect slider_value_rect(const Rect &r) {
    if (r.w <= 0 || r.h <= 0) return {0, 0, 0, 0};

    if (r.h <= 24) { // Detail 模式
        int w = clamp_int((int)std::lround((double)r.w * 0.28), 72, 126);
        int h = clamp_int(r.h, 18, 22);
        int y = r.y + (r.h - h) / 2;
        return {r.x + r.w - w, y, w, h};
    } else { // Simple 模式
        int w = clamp_int((int)std::lround((double)r.w * 0.24), 88, 150);
        if (w > r.w - 10) w = std::max(60, r.w / 2);
        int h = clamp_int(r.h - 2, 22, 28);
        int y = r.y + (r.h - h) / 2;
        return {r.x + r.w - w, y, w, h};
    }
}

void compute_control_layout(int win_width, int win_height, ControlLayout *lo, bool detailed) {
    std::memset(lo, 0, sizeof(*lo));

    lo->panel.x = win_width / 2;
    lo->panel.y = win_height / 2;
    lo->panel.w = win_width - lo->panel.x;
    lo->panel.h = win_height - lo->panel.y;

    int pad_x = clamp_int(lo->panel.w / 26, 8, 26);
    int pad_y = clamp_int(lo->panel.h / 28, 6, 16);
    int gap = clamp_int(lo->panel.h / 68, 2, 8);

    int header_h = clamp_int((int)std::lround((double)lo->panel.h * 0.22), 66, 132);
    int header_x = lo->panel.x + pad_x;
    int header_w = lo->panel.w - 2 * pad_x;
    int header_y = lo->panel.y + pad_y;

    // Header 第1行：Signal Settings（左）+ UTC（右）同行
    int header_title_utc_h = clamp_int((int)std::lround((double)header_h * 0.40), 20, 40);
    int bdt_gpst_gap = std::max(1, gap / 4);
    int header_row_h = clamp_int((header_h - header_title_utc_h - gap - bdt_gpst_gap) / 2, 10, 22);
    int hy = header_y;
    int title_w = clamp_int(header_w / 3, 140, 240);
    int time_w = header_w;
    int time_x = header_x;
    lo->header_title = {header_x, hy, title_w, header_title_utc_h};
    lo->header_utc = {std::max(time_x, header_x + title_w + 6), hy,
                      header_x + header_w - std::max(time_x, header_x + title_w + 6), header_title_utc_h};
    hy += header_title_utc_h + gap;
    lo->header_bdt = {time_x, hy, time_w, header_row_h};
    hy += header_row_h + bdt_gpst_gap;
    lo->header_gpst = {time_x, hy, time_w, header_row_h};
    lo->detail_sats = {0, 0, 0, 0};

    int tab_y = lo->header_gpst.y + lo->header_gpst.h + gap;
    int tab_h = clamp_int(lo->panel.h / 18, 18, 28);
    
    // Tabs will use content frame boundaries (same as action buttons will use)
    // For now, use panel width, but calculate later with proper boundaries
    int tab_x_temp = lo->panel.x;
    int tab_w_temp = lo->panel.w;
    lo->btn_tab_simple = {tab_x_temp, tab_y, tab_w_temp / 2, tab_h};
    lo->btn_tab_detail = {lo->btn_tab_simple.x + lo->btn_tab_simple.w, tab_y, tab_w_temp - lo->btn_tab_simple.w, tab_h};

    // 2. 底部 Action Button：START 在左，EXIT 在右並列（RETURN 在搜尋框旁）
    int action_h = clamp_int(lo->panel.h / 16, 26, 38);
    int action_y = lo->panel.y + lo->panel.h - pad_y - action_h;
    int exit_w = clamp_int(lo->panel.w / 6, 60, 120);
    int action_gap = clamp_int(lo->panel.w / 60, 4, 12);
    int frame_pad_x = 6;
    
    // Calculate aligned boundaries for both tabs and buttons
    int aligned_left = lo->panel.x + pad_x;
    int aligned_right = lo->panel.x + lo->panel.w - pad_x;
    int section_x = std::max(lo->panel.x + 4, aligned_left - frame_pad_x);
    int section_right = std::min(lo->panel.x + lo->panel.w - 4, aligned_right + frame_pad_x);
    int section_w = std::max(0, section_right - section_x);
    
    // Keep controls inset from the frame edge so labels/sliders do not touch the border.
    int content_inset_x = clamp_int(lo->panel.w / 40, 8, 16);
    int content_x = section_x + content_inset_x;
    int content_w = std::max(0, section_w - 2 * content_inset_x);

    // Keep SIMPLE/DETAIL tabs on the frame width; inset only applies to inner controls.
    lo->btn_tab_simple = {section_x, tab_y, section_w / 2, tab_h};
    lo->btn_tab_detail = {lo->btn_tab_simple.x + lo->btn_tab_simple.w, tab_y, section_w - lo->btn_tab_simple.w, tab_h};
    
    lo->btn_return = {0, 0, 0, 0};
    lo->btn_start = {aligned_left, action_y,
                     lo->panel.w - 2 * pad_x - exit_w - action_gap, action_h};
    lo->btn_exit = {lo->btn_start.x + lo->btn_start.w + action_gap, action_y, exit_w, action_h};
    lo->btn_stop = {0, 0, 0, 0}; 

    // 3. 內容區域可用高度
    if (detailed) {
        lo->detail_sats = {header_x, tab_y + tab_h + gap, header_w, clamp_int(lo->panel.h / 36, 8, 14)};
    }
    int content_y = tab_y + tab_h + gap + (detailed ? (lo->detail_sats.h + gap) : 0);
    int content_h = std::max(24, action_y - content_y - gap);
    
    // content_frame: 使用已計算的 section 邊界
    lo->content_frame = {section_x, tab_y, section_w, 
                         std::max(0, action_y - tab_y - gap)};

    int x_l = content_x;
    int full_w = content_w;

    if (detailed) {
        // Detail 模式：以 Simple 為主，去除重複的 SYSTEM/FS，只顯示額外組件（6 槽）
        int col_gap = clamp_int(full_w / 20, 10, 28);
        int col_w = (full_w - col_gap) / 2;
        int x_r = x_l + col_w + col_gap;

        int slot_h = std::max(1, content_h / 6);
        int row_h = clamp_int((int)std::lround((double)slot_h * 0.60), 8, 24);
        int sw_h = clamp_int((int)std::lround((double)slot_h * 0.70), 10, 28);
        row_h = std::min(row_h, std::max(6, slot_h - 4));
        sw_h = std::min(sw_h, std::max(8, slot_h - 2));

        auto slot_y = [&](int idx, int h) {
            int top = content_y + idx * slot_h;
            return top + std::max(0, (slot_h - h) / 2);
        };

        lo->sw_sys = {0, 0, 0, 0};
        lo->fs_slider = {0, 0, 0, 0};

        auto set_slider_2col = [&](Rect *r_left, Rect *r_right, int slot_idx) {
            int sy = slot_y(slot_idx, row_h);
            if (r_left)  *r_left  = {x_l, sy, col_w, row_h};
            if (r_right) *r_right = {x_r, sy, col_w, row_h};
        };

        set_slider_2col(&lo->gain_slider, nullptr, 0);
        set_slider_2col(&lo->cn0_slider, &lo->seed_slider, 1);
        set_slider_2col(&lo->path_v_slider, &lo->path_a_slider, 2);
        set_slider_2col(&lo->prn_slider, &lo->ch_slider, 3);
        lo->tx_slider = {0, 0, 0, 0};

        int sw_y4 = slot_y(4, sw_h);
        lo->sw_fmt = {x_l, sw_y4, col_w, sw_h};
        lo->sw_jam = {0, 0, 0, 0};

        int sw_y5 = slot_y(5, sw_h);
        lo->sw_mode = {x_l, sw_y5, col_w, sw_h};
        int cb_gap = clamp_int(col_w / 18, 6, 12);
        int cb_w = (col_w - 2 * cb_gap) / 3;
        lo->tg_meo  = {x_r, sw_y5, cb_w, sw_h};
        lo->tg_iono = {x_r + cb_w + cb_gap, sw_y5, cb_w, sw_h};
        lo->tg_clk  = {x_r + 2 * (cb_w + cb_gap), sw_y5, cb_w, sw_h};

    } else {
        // Simple 模式使用 4 槽：INTERFERE(SPOOF/JAM) + SYSTEM + FS + TX。
        int slot_h = std::max(1, content_h / 4);
        int sw_h = clamp_int((int)std::lround((double)slot_h * 0.84), 10, 36);
        int row_h = clamp_int((int)std::lround((double)slot_h * 0.72), 8, 30);
        sw_h = std::min(sw_h, std::max(8, slot_h - 2));
        row_h = std::min(row_h, std::max(6, slot_h - 2));

        auto slot_y = [&](int idx, int h) {
            int top = content_y + idx * slot_h;
            return top + std::max(0, (slot_h - h) / 2);
        };

        lo->sw_jam = {x_l, slot_y(0, sw_h), full_w, sw_h};
        lo->sw_sys = {x_l, slot_y(1, sw_h), full_w, sw_h};
        lo->fs_slider = {x_l, slot_y(2, row_h), full_w, row_h};
        lo->gain_slider = {0, 0, 0, 0};
        lo->tx_slider = {x_l, slot_y(3, row_h), full_w, row_h};
    }
}

int control_slider_hit_test(int x, int y, int win_width, int win_height, bool detailed) {
    ControlLayout lo; compute_control_layout(win_width, win_height, &lo, detailed);
    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(lo.tx_slider, x, y) && !rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(lo.gain_slider, x, y) && !rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(lo.fs_slider, x, y) && !rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (detailed && rect_hit(lo.cn0_slider, x, y) && !rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (detailed && rect_hit(lo.seed_slider, x, y) && !rect_hit(slider_value_rect(lo.seed_slider), x, y)) return CTRL_SLIDER_SEED;
    if (detailed && rect_hit(lo.prn_slider, x, y) && !rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (detailed && rect_hit(lo.path_v_slider, x, y) && !rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (detailed && rect_hit(lo.path_a_slider, x, y) && !rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (detailed && rect_hit(lo.ch_slider, x, y) && !rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}

int control_value_hit_test(int x, int y, int win_width, int win_height, bool detailed) {
    ControlLayout lo; compute_control_layout(win_width, win_height, &lo, detailed);
    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (detailed && rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (detailed && rect_hit(slider_value_rect(lo.seed_slider), x, y)) return CTRL_SLIDER_SEED;
    if (detailed && rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (detailed && rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (detailed && rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (detailed && rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}