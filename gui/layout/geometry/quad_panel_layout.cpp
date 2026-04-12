#include "gui/layout/geometry/quad_panel_layout.h"

#include <algorithm>
#include <cmath>

// 縮小時的最小寬度比例（25% = 0.25）
static const double MIN_WIDTH_RATIO = 0.25;
// 展開時的最大寬度比例（70% = 0.70）
static const double MAX_WIDTH_RATIO = 0.70;

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

// 新函數：支持動態展開的左下面板位置計算
void get_rb_lq_panel_rect_expanded(int win_width,
                                   int win_height,
                                   int *panel_x,
                                   int *panel_y,
                                   int *panel_w,
                                   int *panel_h,
                                   bool lower_half,
                                   double expand_progress)
{
    int rb_x = win_width / 2;
    int rb_y = win_height / 2;
    int rb_w = win_width - rb_x;
    int rb_h = win_height - rb_y;

    // 將 expand_progress 限制在 0.0 ~ 1.0
    double progress = std::max(0.0, std::min(1.0, expand_progress));

    // 計算寬度比例：從 MIN_WIDTH_RATIO 到 MAX_WIDTH_RATIO
    double width_ratio = MIN_WIDTH_RATIO + (MAX_WIDTH_RATIO - MIN_WIDTH_RATIO) * progress;
    
    // 計算面板寬度
    int panel_width = (int)std::round(rb_w * width_ratio);

    *panel_x = rb_x;
    *panel_y = rb_y + (lower_half ? (rb_h / 2) : 0);
    *panel_w = panel_width;
    *panel_h = rb_h / 2;
}

// 新函數：支持動態展開的右下面板位置計算
void get_rb_rq_panel_rect_expanded(int win_width,
                                   int win_height,
                                   int *panel_x,
                                   int *panel_y,
                                   int *panel_w,
                                   int *panel_h,
                                   bool lower_half,
                                   double expand_progress)
{
    int rb_x = win_width / 2;
    int rb_y = win_height / 2;
    int rb_w = win_width - rb_x;
    int rb_h = win_height - rb_y;

    // 將 expand_progress 限制在 0.0 ~ 1.0
    double progress = std::max(0.0, std::min(1.0, expand_progress));

    // 計算寬度比例：從 MIN_WIDTH_RATIO 到 MAX_WIDTH_RATIO
    double width_ratio = MIN_WIDTH_RATIO + (MAX_WIDTH_RATIO - MIN_WIDTH_RATIO) * progress;
    
    // 計算面板寬度
    int panel_width = (int)std::round(rb_w * width_ratio);

    // 右下面板右對齊
    *panel_x = rb_x + rb_w - panel_width;
    *panel_y = rb_y + (lower_half ? (rb_h / 2) : 0);
    *panel_w = panel_width;
    *panel_h = rb_h / 2;
}