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

enum class ControlTemplateKind {
    Landscape,
    Portrait,
};

struct ControlTemplateInfo {
    ControlTemplateKind kind;
    double aspect;
    bool ultra_wide;
};

static ControlTemplateInfo control_template_info(int panel_w, int panel_h) {
    ControlTemplateInfo info{};
    info.aspect = (panel_h > 0) ? ((double)panel_w / (double)panel_h) : 1.0;
    info.kind = (info.aspect < 1.0) ? ControlTemplateKind::Portrait
                                    : ControlTemplateKind::Landscape;
    info.ultra_wide = (info.aspect >= 1.85);
    return info;
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
        int min_w = (r.w < 200) ? 56 : 72;
        int w = clamp_int((int)std::lround((double)r.w * 0.28), min_w, 126);
        int h = clamp_int(r.h, 18, 22);
        int y = r.y + (r.h - h) / 2;
        return {r.x + r.w - w, y, w, h};
    } else { // Simple 模式
        int min_w = (r.w < 300) ? 70 : 88;
        int w = clamp_int((int)std::lround((double)r.w * 0.24), min_w, 150);
        if (w > r.w - 10) w = std::max(60, r.w / 2);
        int h = clamp_int(r.h - 2, 22, 28);
        int y = r.y + (r.h - h) / 2;
        return {r.x + r.w - w, y, w, h};
    }
}

void compute_control_layout(int win_width, int win_height, ControlLayout *lo, bool detailed,
                            bool single_system_sat_layout) {
    std::memset(lo, 0, sizeof(*lo));

    lo->panel.x = win_width / 2;
    lo->panel.y = win_height / 2;
    lo->panel.w = win_width - lo->panel.x;
    lo->panel.h = win_height - lo->panel.y;

    ControlTemplateInfo tpl = control_template_info(lo->panel.w, lo->panel.h);
    const bool portrait = (tpl.kind == ControlTemplateKind::Portrait);
    double panel_scale = std::min((double)lo->panel.w / 620.0,
                                  (double)lo->panel.h / 360.0);
    panel_scale = clamp_double(panel_scale, 0.82, 1.12);
    if (portrait) {
        panel_scale *= 0.96;
    } else if (tpl.ultra_wide) {
        panel_scale *= 1.03;
    }

    int pad_x = clamp_int((int)std::lround((double)lo->panel.w / (portrait ? 28.0 : 26.0) * panel_scale),
                          8, portrait ? 22 : 26);
    int pad_y = clamp_int((int)std::lround((double)lo->panel.h / (portrait ? 30.0 : 28.0) * panel_scale),
                          6, portrait ? 18 : 16);
    int gap = clamp_int((int)std::lround((double)lo->panel.h / (portrait ? 72.0 : 68.0) * panel_scale),
                        2, portrait ? 7 : 8);

    int header_h = clamp_int((int)std::lround((double)lo->panel.h * (portrait ? 0.19 : 0.22)),
                             portrait ? 58 : 66, 132);
    int header_x = lo->panel.x + pad_x;
    int header_w = lo->panel.w - 2 * pad_x;
    int header_y = lo->panel.y + pad_y;

    // Header 第1行：Signal Settings（左）+ UTC（右）同行
    int header_title_utc_h = clamp_int((int)std::lround((double)header_h * (portrait ? 0.38 : 0.40)), 20, 40);
    int bdt_gpst_gap = std::max(1, gap / 4);
    int header_row_h = clamp_int((header_h - header_title_utc_h - gap - bdt_gpst_gap) / 2, 10, portrait ? 24 : 22);
    int hy = header_y;
    // Header 第1行：固定 GEAR(左) 與 UTC(右) 錨點，Signal Settings 永遠在兩者正中間動態調整。
    int row_w = std::max(1, header_w);
    int side_sep = std::max(6, (int)std::lround((double)row_w * 0.018));

    int gear_size = clamp_int((int)std::lround((double)header_title_utc_h * 0.88), 12, 64);
    lo->header_gear = {header_x, hy + std::max(0, (header_title_utc_h - gear_size) / 2),
                       gear_size, gear_size};

    int utc_w = clamp_int((int)std::lround((double)row_w * 0.70),
                          std::max(120, row_w / 2), std::max(120, row_w - gear_size - side_sep * 2));
    int utc_x = header_x + row_w - utc_w;
    lo->header_utc = {utc_x, hy, utc_w, header_title_utc_h};

    int title_left = lo->header_gear.x + lo->header_gear.w + side_sep;
    int title_right = lo->header_utc.x - side_sep;
    int title_span = std::max(24, title_right - title_left);
    int title_pref_w = clamp_int((int)std::lround((double)row_w * 0.25), 56, title_span);
    int gear_center = lo->header_gear.x + lo->header_gear.w / 2;
    int utc_center = lo->header_utc.x + lo->header_utc.w / 2;
    int mid_center = (gear_center + utc_center) / 2;
    int title_x = mid_center - title_pref_w / 2;
    int title_min_x = title_left;
    int title_max_x = title_right - title_pref_w;
    if (title_max_x < title_min_x) {
        title_max_x = title_min_x;
        title_pref_w = std::max(24, title_span);
    }
    title_x = clamp_int(title_x, title_min_x, title_max_x);
    lo->header_title = {title_x, hy, title_pref_w, header_title_utc_h};
    int time_x = header_x;
    int time_w = header_w;
    hy += header_title_utc_h + gap;
    lo->header_bdt = {time_x, hy, time_w, header_row_h};
    hy += header_row_h + bdt_gpst_gap;
    lo->header_gpst = {time_x, hy, time_w, header_row_h};
    lo->detail_sats = {0, 0, 0, 0};

    int tab_y = lo->header_gpst.y + lo->header_gpst.h + gap;
    int tab_h = clamp_int((int)std::lround((double)lo->panel.h * (portrait ? 0.042 : 0.040)),
                          portrait ? 48 : 44, portrait ? 64 : 60);
    
    // Tabs will use content frame boundaries (same as action buttons will use)
    // For now, use panel width, but calculate later with proper boundaries
    int tab_x_temp = lo->panel.x;
    int tab_w_temp = lo->panel.w;
    lo->btn_tab_simple = {tab_x_temp, tab_y, tab_w_temp / 2, tab_h};
    lo->btn_tab_detail = {lo->btn_tab_simple.x + lo->btn_tab_simple.w, tab_y, tab_w_temp - lo->btn_tab_simple.w, tab_h};

    // 2. 底部 Action Button：START 在左，EXIT 在右並列（RETURN 在搜尋框旁）
    int action_h = clamp_int((int)std::lround((double)lo->panel.h * (portrait ? 0.070 : 0.062)), 24, 38);
    int action_y = lo->panel.y + lo->panel.h - pad_y - action_h;
    int exit_w = clamp_int((int)std::lround((double)lo->panel.w * (portrait ? 0.17 : 0.16)), 60, 120);
    // Narrow landscape + Detail: reduce action button footprint to avoid squeezing
    // FORMAT / MODE / MEO rows near the bottom.
    if (detailed && !portrait && lo->panel.w < 520) {
        action_h = clamp_int((int)std::lround((double)action_h * 0.86), 20, 34);
        exit_w = clamp_int((int)std::lround((double)exit_w * 0.88), 54, 108);
        action_y = lo->panel.y + lo->panel.h - pad_y - action_h;
    }
    int action_gap = clamp_int((int)std::lround((double)lo->panel.w / (portrait ? 54.0 : 60.0)), 4, 12);
    int frame_pad_x = 6;
    
    // Calculate aligned boundaries for both tabs and buttons
    int aligned_left = lo->panel.x + pad_x;
    int aligned_right = lo->panel.x + lo->panel.w - pad_x;
    int section_x = std::max(lo->panel.x + 4, aligned_left - frame_pad_x);
    int section_right = std::min(lo->panel.x + lo->panel.w - 4, aligned_right + frame_pad_x);
    int section_w = std::max(0, section_right - section_x);
    
    // Keep controls inset from the frame edge so labels/sliders do not touch the border.
    int content_inset_x = clamp_int((int)std::lround((double)lo->panel.w / (portrait ? 46.0 : 40.0)), 8, 16);
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
    int detail_base_y = tab_y + tab_h + gap;
    if (detailed) {
        lo->detail_sats = {0, 0, 0, 0};
    }
    int content_y = detail_base_y;
    int content_h = std::max(24, action_y - content_y - gap);
    
    // content_frame: 使用已計算的 section 邊界
    lo->content_frame = {section_x, tab_y, section_w, 
                         std::max(0, action_y - tab_y - gap)};

    int x_l = content_x;
    int full_w = content_w;

    if (detailed) {
        // Detail 模式固定比例：SAT 15% / SIGNAL 15% / PATH 15% / PRN+MATCH 15%
        // / FORMAT+MODE(+MEO/IONO/EXT) 35% / 底部保留 5%。
        int detail_h_total = std::max(24, action_y - detail_base_y - gap);
        double sat_ratio = single_system_sat_layout ? 0.075 : 0.15;
        double signal_ratio = single_system_sat_layout ? 0.15 : 0.12;
        double path_ratio = single_system_sat_layout ? 0.15 : 0.12;
        double prn_ratio = single_system_sat_layout ? 0.15 : 0.12;
        double switch_ratio = single_system_sat_layout ? 0.425 : 0.44;
        int sat_h = (int)std::lround((double)detail_h_total * sat_ratio);
        int signal_h = (int)std::lround((double)detail_h_total * signal_ratio);
        int path_h = (int)std::lround((double)detail_h_total * path_ratio);
        int prn_h = (int)std::lround((double)detail_h_total * prn_ratio);
        int sw_block_h = (int)std::lround((double)detail_h_total * switch_ratio);
        int reserve_h = std::max(1, detail_h_total - sat_h - signal_h - path_h - prn_h - sw_block_h);

        int y_cursor = detail_base_y;
        lo->detail_sats = {header_x, y_cursor, header_w, std::max(12, sat_h)};
        y_cursor += lo->detail_sats.h;

        int col_gap = clamp_int(full_w / 20, 8, 24);
        int col_w = std::max(24, (full_w - col_gap) / 2);
        int x_r = x_l + col_w + col_gap;

        int row_signal_h = std::max(12, signal_h);
        lo->gain_slider = {x_l, y_cursor, col_w, row_signal_h};
        lo->cn0_slider = {x_r, y_cursor, std::max(24, full_w - col_w - col_gap), row_signal_h};
        y_cursor += row_signal_h;

        int row_path_h = std::max(12, path_h);
        lo->path_v_slider = {x_l, y_cursor, col_w, row_path_h};
        lo->path_a_slider = {x_r, y_cursor, std::max(24, full_w - col_w - col_gap), row_path_h};
        y_cursor += row_path_h;

        int row_prn_h = std::max(12, prn_h);
        lo->prn_slider = {x_l, y_cursor, col_w, row_prn_h};
        lo->ch_slider = {x_r, y_cursor, std::max(24, full_w - col_w - col_gap), row_prn_h};
        y_cursor += row_prn_h;

        int block_h = std::max(16, sw_block_h);
        int inner_gap = clamp_int(block_h / 12, 1, 10);
        int sw_each_h = std::max(10, (block_h - inner_gap) / 2);
        lo->sw_fmt = {x_l, y_cursor, col_w, sw_each_h};
        lo->sw_mode = {x_l, y_cursor + sw_each_h + inner_gap, col_w, sw_each_h};

        int cb_gap = clamp_int(col_w / 18, 4, 12);
        int cb_w = std::max(18, (col_w - 2 * cb_gap) / 3);
        int cb_y = y_cursor + std::max(0, (block_h - sw_each_h) / 2);
        lo->tg_meo  = {x_r, cb_y, cb_w, sw_each_h};
        lo->tg_iono = {x_r + cb_w + cb_gap, cb_y, cb_w, sw_each_h};
        lo->tg_clk  = {x_r + 2 * (cb_w + cb_gap), cb_y,
                       std::max(18, std::max(24, full_w - col_w - col_gap) - 2 * (cb_w + cb_gap)), sw_each_h};
        y_cursor += block_h;

        // 關閉 Detail 不使用的 simple-only controls。
        lo->sw_sys = {0, 0, 0, 0};
        lo->fs_slider = {0, 0, 0, 0};
        lo->tx_slider = {0, 0, 0, 0};
        lo->sw_jam = {0, 0, 0, 0};
        lo->seed_slider = {0, 0, 0, 0};

        (void)reserve_h;

    } else {
        // Simple 模式使用 4 槽：INTERFERE(SPOOF/JAM) + SYSTEM + FS + TX。
        int row_gap = clamp_int(gap, 2, portrait ? 8 : 10);
        const int jam_sys_gap = 1;
        int alloc_h = std::max(24, content_h - (jam_sys_gap + row_gap + row_gap));
        int group_switch_h = (int)std::lround((double)alloc_h * 0.45);
        int fs_h = (int)std::lround((double)alloc_h * 0.35);
        int tx_h = std::max(1, alloc_h - group_switch_h - fs_h);
        int sw_jam_h = std::max(1, group_switch_h / 2);
        int sw_sys_h = std::max(1, group_switch_h - sw_jam_h);

        int y_cursor = content_y;
        lo->sw_jam = {x_l, y_cursor, full_w, sw_jam_h};
        y_cursor += sw_jam_h + jam_sys_gap;
        lo->sw_sys = {x_l, y_cursor, full_w, sw_sys_h};
        y_cursor += sw_sys_h + row_gap;
        lo->fs_slider = {x_l, y_cursor, full_w, fs_h};
        lo->gain_slider = {0, 0, 0, 0};
        y_cursor += fs_h + row_gap;
        lo->tx_slider = {x_l, y_cursor, full_w, tx_h};
    }
}

int control_slider_hit_test(int x, int y, int win_width, int win_height, bool detailed,
                            bool single_system_sat_layout) {
    ControlLayout lo; compute_control_layout(win_width, win_height, &lo, detailed, single_system_sat_layout);
    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(lo.tx_slider, x, y) && !rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(lo.gain_slider, x, y) && !rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(lo.fs_slider, x, y) && !rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (detailed && rect_hit(lo.cn0_slider, x, y) && !rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (detailed && rect_hit(lo.prn_slider, x, y) && !rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (detailed && rect_hit(lo.path_v_slider, x, y) && !rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (detailed && rect_hit(lo.path_a_slider, x, y) && !rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (detailed && rect_hit(lo.ch_slider, x, y) && !rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}

int control_value_hit_test(int x, int y, int win_width, int win_height, bool detailed,
                           bool single_system_sat_layout) {
    ControlLayout lo; compute_control_layout(win_width, win_height, &lo, detailed, single_system_sat_layout);
    if (!rect_hit(lo.panel, x, y)) return -1;
    if (rect_hit(slider_value_rect(lo.tx_slider), x, y)) return CTRL_SLIDER_TX;
    if (rect_hit(slider_value_rect(lo.gain_slider), x, y)) return CTRL_SLIDER_GAIN;
    if (rect_hit(slider_value_rect(lo.fs_slider), x, y)) return CTRL_SLIDER_FS;
    if (detailed && rect_hit(slider_value_rect(lo.cn0_slider), x, y)) return CTRL_SLIDER_CN0;
    if (detailed && rect_hit(slider_value_rect(lo.prn_slider), x, y)) return CTRL_SLIDER_PRN;
    if (detailed && rect_hit(slider_value_rect(lo.path_v_slider), x, y)) return CTRL_SLIDER_PATH_V;
    if (detailed && rect_hit(slider_value_rect(lo.path_a_slider), x, y)) return CTRL_SLIDER_PATH_A;
    if (detailed && rect_hit(slider_value_rect(lo.ch_slider), x, y)) return CTRL_SLIDER_CH;
    return -1;
}