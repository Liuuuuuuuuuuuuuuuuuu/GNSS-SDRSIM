#include "gui/quad_panel_layout.h"

void get_rb_lq_panel_rect(int win_width,
                          int win_height,
                          int *panel_x,
                          int *panel_y,
                          int *panel_w,
                          int *panel_h,
                          bool lower_half)
{
    int rb_x = win_width / 2;
    int rb_y = win_height / 2;
    int rb_w = win_width - rb_x;
    int rb_h = win_height - rb_y;

    *panel_x = rb_x;
    *panel_y = rb_y + (lower_half ? (rb_h / 2) : 0);
    *panel_w = rb_w / 2;
    *panel_h = rb_h / 2;
}

void get_rb_rq_panel_rect(int win_width,
                          int win_height,
                          int *panel_x,
                          int *panel_y,
                          int *panel_w,
                          int *panel_h,
                          bool lower_half)
{
    int rb_x = win_width / 2;
    int rb_y = win_height / 2;
    int rb_w = win_width - rb_x;
    int rb_h = win_height - rb_y;

    *panel_x = rb_x + rb_w / 2;
    *panel_y = rb_y + (lower_half ? (rb_h / 2) : 0);
    *panel_w = rb_w / 2;
    *panel_h = rb_h / 2;
}
