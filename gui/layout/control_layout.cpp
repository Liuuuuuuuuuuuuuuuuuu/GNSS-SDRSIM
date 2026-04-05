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

void compute_control_layout(int win_width, int win_height, ControlLayout *lo, bool detailed) {
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
    int gear_size = clamp_int(header_title_utc_h - 4, 18, portrait ? 28 : 30);
    lo->header_gear = {header_x, hy + std::max(0, (header_title_utc_h - gear_size) / 2),
                       gear_size, gear_size};
    int title_x = lo->header_gear.x + lo->header_gear.w + std::max(4, gap);
    int min_title_w = portrait ? 72 : ((header_w < 340) ? 72 : 120);
    int title_w = portrait
        ? clamp_int(header_w / 4, min_title_w, 160)
        : clamp_int(header_w / 3, min_title_w, 240);
    if (title_x + title_w > header_x + header_w) {
        title_w = std::max(40, header_x + header_w - title_x);
    }
    int time_w = header_w;
    int time_x = header_x;
    lo->header_title = {title_x, hy, title_w, header_title_utc_h};
    int utc_x = std::max(time_x, title_x + title_w + (portrait ? 4 : 6));
    lo->header_utc = {utc_x, hy,
                      header_x + header_w - utc_x, header_title_utc_h};
    hy += header_title_utc_h + gap;
    lo->header_bdt = {time_x, hy, time_w, header_row_h};
    hy += header_row_h + bdt_gpst_gap;
    lo->header_gpst = {time_x, hy, time_w, header_row_h};
    lo->detail_sats = {0, 0, 0, 0};

    int tab_y = lo->header_gpst.y + lo->header_gpst.h + gap;
    int tab_h = clamp_int((int)std::lround((double)lo->panel.h * (portrait ? 0.021 : 0.020)),
                          portrait ? 24 : 22, portrait ? 32 : 30);
    
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
    if (detailed) {
        int sats_h = clamp_int((int)std::lround((double)lo->panel.h * (portrait ? 0.032 : 0.028)),
                               portrait ? 18 : 14, portrait ? 26 : 18);
        lo->detail_sats = {header_x, tab_y + tab_h + gap, header_w, sats_h};
    }
    int content_y = tab_y + tab_h + gap + (detailed ? (lo->detail_sats.h + gap) : 0);
    int content_h = std::max(24, action_y - content_y - gap);
    
    // content_frame: 使用已計算的 section 邊界
    lo->content_frame = {section_x, tab_y, section_w, 
                         std::max(0, action_y - tab_y - gap)};

    int x_l = content_x;
    int full_w = content_w;

    if (detailed) {
        bool compact_detail = portrait || (full_w < 430) || (content_h < 250);
        if (compact_detail) {
            int row_gap = clamp_int(gap, 2, portrait ? 7 : 8);
            int mode_bottom_margin = clamp_int(lo->panel.h / (portrait ? 42 : 38),
                                               portrait ? 5 : 6, portrait ? 12 : 14);
            int avail_h = std::max(24, content_h - row_gap * (portrait ? 7 : 8) - mode_bottom_margin);

            bool split_prn_ch = (!portrait && full_w >= 280);
            int slider_rows = split_prn_ch ? 5 : 6;
            int switch_rows = 3;
            double switch_w = portrait ? 1.28 : 1.35;
            double total_w = (double)slider_rows + switch_w * (double)switch_rows;
            double unit_h = (total_w > 0.0) ? ((double)avail_h / total_w) : 0.0;

            int row_h = clamp_int((int)std::lround(unit_h),
                                  clamp_int(lo->panel.h / (portrait ? 54 : 56), 14, portrait ? 22 : 18),
                                  clamp_int(lo->panel.h / (portrait ? 17 : 20), 22, portrait ? 38 : 34));
            int sw_h = clamp_int((int)std::lround(unit_h * switch_w),
                                 clamp_int(lo->panel.h / (portrait ? 26 : 30), 22, portrait ? 32 : 28),
                                 clamp_int(lo->panel.h / (portrait ? 11 : 12), 36, portrait ? 58 : 54));

            int y_cursor = content_y;

            lo->sw_sys = {0, 0, 0, 0};
            lo->fs_slider = {0, 0, 0, 0};
            lo->tx_slider = {0, 0, 0, 0};
            lo->sw_jam = {0, 0, 0, 0};

            auto set_slider_full = [&](Rect *r) {
                if (r) *r = {x_l, y_cursor, full_w, row_h};
                y_cursor += row_h + row_gap;
            };

            set_slider_full(&lo->gain_slider);
            set_slider_full(&lo->cn0_slider);
            set_slider_full(&lo->seed_slider);
            set_slider_full(&lo->path_v_slider);
            set_slider_full(&lo->path_a_slider);

            if (split_prn_ch) {
                int pair_gap = clamp_int(full_w / 24, 8, 14);
                int pair_w = std::max(24, (full_w - pair_gap) / 2);
                lo->prn_slider = {x_l, y_cursor, pair_w, row_h};
                lo->ch_slider = {x_l + pair_w + pair_gap, y_cursor,
                                 std::max(24, full_w - pair_w - pair_gap), row_h};
                y_cursor += row_h + row_gap;
            } else {
                set_slider_full(&lo->prn_slider);
                set_slider_full(&lo->ch_slider);
            }

            lo->sw_fmt = {x_l, y_cursor, full_w, sw_h};
            y_cursor += sw_h + row_gap;

            lo->sw_mode = {x_l, y_cursor, full_w, sw_h};
            y_cursor += sw_h + row_gap;

            int cb_gap = clamp_int(full_w / (portrait ? 28 : 24), 6, 12);
            int cb_w = std::max(18, (full_w - 2 * cb_gap) / 3);
            lo->tg_meo  = {x_l, y_cursor, cb_w, sw_h};
            lo->tg_iono = {x_l + cb_w + cb_gap, y_cursor, cb_w, sw_h};
            lo->tg_clk  = {x_l + 2 * (cb_w + cb_gap), y_cursor,
                           std::max(18, full_w - 2 * (cb_w + cb_gap)), sw_h};
            return;
        }

        // Detail 模式：以 Simple 為主，去除重複的 SYSTEM/FS，只顯示額外組件（6 槽）
        int col_gap = clamp_int(full_w / 20, 10, 28);
        int col_w = (full_w - col_gap) / 2;
        int x_r = x_l + col_w + col_gap;

        // 讓 FORMAT/MODE 這類 switch 列比一般 slider 列更高，避免 caption 放大後壓縮內容。
        int row_gap = clamp_int(gap, 2, 10);
        // Keep a dedicated bottom margin so MODE switch does not touch panel border.
        int mode_bottom_margin = clamp_int(lo->panel.h / 36, 8, 18);
        int avail_h = std::max(24, content_h - row_gap * 5 - mode_bottom_margin);
        double slider_w = 1.0;
        double switch_w = 1.50;
        double total_w = slider_w * 4.0 + switch_w * 2.0;
        double unit_h = (total_w > 0.0) ? ((double)avail_h / total_w) : 0.0;

        int row_h = clamp_int((int)std::lround(unit_h * slider_w),
                              clamp_int(lo->panel.h / 48, 14, 20),
                              clamp_int(lo->panel.h / 18, 22, 36));
        int sw_h = clamp_int((int)std::lround(unit_h * switch_w),
                             clamp_int(lo->panel.h / 26, 22, 32),
                             clamp_int(lo->panel.h / 12, 34, 56));

        int used_h = row_h * 4 + sw_h * 2;
        if (used_h > avail_h) {
            int over = used_h - avail_h;
            int slider_floor = clamp_int(lo->panel.h / 56, 12, 18);
            int switch_floor = clamp_int(lo->panel.h / 30, 20, 28);
            int cut_slider = std::min(over, std::max(0, row_h - slider_floor) * 4);
            row_h -= cut_slider / 4;
            over -= cut_slider;
            if (over > 0) {
                int cut_switch = std::min(over, std::max(0, sw_h - switch_floor) * 2);
                sw_h -= cut_switch / 2;
            }
        }
        if (used_h < avail_h) {
            int extra = avail_h - used_h;
            // 優先把額外高度分配給 switch，確保 caption 放大時不會把 segment 擠扁。
            int add_sw = extra * 2 / 3;
            int add_row = extra - add_sw;
            sw_h += add_sw / 2;
            row_h += add_row / 4;
        }

        int y_cursor = content_y;

        lo->sw_sys = {0, 0, 0, 0};
        lo->fs_slider = {0, 0, 0, 0};

        auto set_slider_2col = [&](Rect *r_left, Rect *r_right) {
            int sy = y_cursor;
            if (r_left)  *r_left  = {x_l, sy, col_w, row_h};
            if (r_right) *r_right = {x_r, sy, col_w, row_h};
            y_cursor += row_h + row_gap;
        };

        set_slider_2col(&lo->gain_slider, nullptr);
        set_slider_2col(&lo->cn0_slider, &lo->seed_slider);
        set_slider_2col(&lo->path_v_slider, &lo->path_a_slider);
        set_slider_2col(&lo->prn_slider, &lo->ch_slider);
        lo->tx_slider = {0, 0, 0, 0};

        int sw_y4 = y_cursor;
        lo->sw_fmt = {x_l, sw_y4, col_w, sw_h};
        lo->sw_jam = {0, 0, 0, 0};
        y_cursor += sw_h + row_gap;

        int sw_y5 = y_cursor;
        lo->sw_mode = {x_l, sw_y5, col_w, sw_h};
        int cb_gap = clamp_int(col_w / 18, 6, 12);
        int cb_w = (col_w - 2 * cb_gap) / 3;
        lo->tg_meo  = {x_r, sw_y5, cb_w, sw_h};
        lo->tg_iono = {x_r + cb_w + cb_gap, sw_y5, cb_w, sw_h};
        lo->tg_clk  = {x_r + 2 * (cb_w + cb_gap), sw_y5, cb_w, sw_h};

    } else {
        // Simple 模式使用 4 槽：INTERFERE(SPOOF/JAM) + SYSTEM + FS + TX。
        int row_gap = clamp_int(gap, 2, portrait ? 8 : 10);
        int avail_h = std::max(24, content_h - row_gap * 3);
        double slider_w = 1.0;
        double switch_w = portrait ? 1.16 : 1.45;
        double total_w = slider_w * 2.0 + switch_w * 2.0;
        double unit_h = (total_w > 0.0) ? ((double)avail_h / total_w) : 0.0;

        int row_h = clamp_int((int)std::lround(unit_h * slider_w),
                      clamp_int(lo->panel.h / (portrait ? 42 : 42), 18, portrait ? 26 : 22),
                      clamp_int(lo->panel.h / (portrait ? 14 : 16), 26, portrait ? 42 : 40));
        int sw_h = clamp_int((int)std::lround(unit_h * switch_w),
                     clamp_int(lo->panel.h / (portrait ? 24 : 24), 20, portrait ? 30 : 34),
                     clamp_int(lo->panel.h / (portrait ? 11 : 11), 30, portrait ? 48 : 58));

        int used_h = row_h * 2 + sw_h * 2;
        if (used_h > avail_h) {
            int over = used_h - avail_h;
            int row_floor = clamp_int(lo->panel.h / 52, 14, 18);
            int sw_floor = clamp_int(lo->panel.h / 30, 20, 28);
            int cut_row = std::min(over, std::max(0, row_h - row_floor) * 2);
            row_h -= cut_row / 2;
            over -= cut_row;
            if (over > 0) {
                int cut_sw = std::min(over, std::max(0, sw_h - sw_floor) * 2);
                sw_h -= cut_sw / 2;
            }
        }
        if (used_h < avail_h) {
            int extra = avail_h - used_h;
            int add_sw = extra * 2 / 3;
            int add_row = extra - add_sw;
            sw_h += add_sw / 2;
            row_h += add_row / 2;
        }

        int y_cursor = content_y;
        lo->sw_jam = {x_l, y_cursor, full_w, sw_h};
        y_cursor += sw_h + row_gap;
        lo->sw_sys = {x_l, y_cursor, full_w, sw_h};
        y_cursor += sw_h + row_gap;
        lo->fs_slider = {x_l, y_cursor, full_w, row_h};
        lo->gain_slider = {0, 0, 0, 0};
        y_cursor += row_h + row_gap;
        lo->tx_slider = {x_l, y_cursor, full_w, row_h};
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