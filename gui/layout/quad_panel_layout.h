#ifndef QUAD_PANEL_LAYOUT_H
#define QUAD_PANEL_LAYOUT_H

// 原始函數（固定大小）
void get_rb_lq_panel_rect(int win_width,
                          int win_height,
                          int *panel_x,
                          int *panel_y,
                          int *panel_w,
                          int *panel_h,
                          bool lower_half);

void get_rb_rq_panel_rect(int win_width,
                          int win_height,
                          int *panel_x,
                          int *panel_y,
                          int *panel_w,
                          int *panel_h,
                          bool lower_half);

// 新函數：支持動態展開的面板位置計算
// expand_progress: 0.0 = 完全縮小，1.0 = 完全展開
void get_rb_lq_panel_rect_expanded(int win_width,
                                   int win_height,
                                   int *panel_x,
                                   int *panel_y,
                                   int *panel_w,
                                   int *panel_h,
                                   bool lower_half,
                                   double expand_progress);

void get_rb_rq_panel_rect_expanded(int win_width,
                                   int win_height,
                                   int *panel_x,
                                   int *panel_y,
                                   int *panel_w,
                                   int *panel_h,
                                   bool lower_half,
                                   double expand_progress);

#endif
