#ifndef QUAD_PANEL_LAYOUT_H
#define QUAD_PANEL_LAYOUT_H

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

#endif
