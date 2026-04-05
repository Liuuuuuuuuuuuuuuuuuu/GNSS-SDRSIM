#include "gui/map/map_hover_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"
#include "gui/layout/quad_panel_layout.h"

#include <QFontMetrics>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

int set_result(QString *text, QRect *anchor, int id, const QString &msg,
               const QRect &r) {
  if (text) *text = msg;
  if (anchor) *anchor = r;
  return id;
}

} // namespace

int map_hover_region_for_pos(const QPoint &pos, const MapHoverHelpInput &in,
                             QString *text, QRect *anchor) {
  if (in.tutorial_overlay_visible) return -1;

  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.ctrl.show_detailed_ctrl);

  if (!in.dark_mode_btn_rect.isEmpty() && in.dark_mode_btn_rect.contains(pos)) {
    return set_result(text, anchor, 10,
                      in.dark_map_mode
                          ? QString("LIGHT: switch map theme back to bright mode.")
                          : QString("DARK: switch map theme to dark mode for low-light viewing."),
                      in.dark_mode_btn_rect);
  }
  if (!in.nfz_btn_rect.isEmpty() && in.nfz_btn_rect.contains(pos)) {
    return set_result(text, anchor, 11,
                      in.nfz_on
                          ? QString("NFZ ON: DJI no-fly zones are visible. Click to hide them.")
                          : QString("NFZ OFF: click to show DJI no-fly zones on the map."),
                      in.nfz_btn_rect);
  }
  if (!in.tutorial_toggle_rect.isEmpty() && in.tutorial_toggle_rect.contains(pos)) {
    return set_result(text, anchor, 12,
                      in.ctrl.show_detailed_ctrl
                          ? QString("GUIDE ON: guided overlay is enabled. Click to disable tutorial guidance.")
                          : QString("GUIDE OFF: click to enable step-by-step guided tutorial."),
                      in.tutorial_toggle_rect);
  }
  if (in.search_box_visible && in.search_box_rect.contains(pos)) {
    return set_result(text, anchor, 17,
                      "Search box: enter place/address/lat,lon and press Enter to jump map center.",
                      in.search_box_rect);
  }
  if (in.show_search_return && !in.search_return_btn_rect.isEmpty() &&
      in.search_return_btn_rect.contains(pos)) {
    return set_result(text, anchor, 13,
                      "RETURN: restore the map center and zoom from before the last search.",
                      in.search_return_btn_rect);
  }
  if (in.ctrl.running_ui && !in.back_btn_rect.isEmpty() && in.back_btn_rect.contains(pos)) {
    return set_result(text, anchor, 14,
                      "BACK: remove the last confirmed path segment that has not started yet.",
                      in.back_btn_rect);
  }
  if (in.ctrl.running_ui && !in.osm_stop_btn_rect.isEmpty() &&
      in.osm_stop_btn_rect.contains(pos)) {
    return set_result(text, anchor, 15,
                      "STOP SIMULATION: end the active run and return to control setup.",
                      in.osm_stop_btn_rect);
  }
  if (in.ctrl.running_ui && !in.osm_runtime_rect.isEmpty() &&
      in.osm_runtime_rect.contains(pos)) {
    return set_result(text, anchor, 16,
                      "RUN TIME: elapsed time since initialization/transmit start; useful for timing checks.",
                      in.osm_runtime_rect);
  }

  if (in.ctrl.running_ui) {
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
    QRect spectrum_rect(panel_x, panel_y, panel_w, panel_h);
    if (spectrum_rect.contains(pos)) {
      return set_result(text, anchor, 60, "Spectrum: power vs frequency (current frame).", spectrum_rect);
    }

    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
    QRect waterfall_rect(panel_x, panel_y, panel_w, panel_h);
    if (waterfall_rect.contains(pos)) {
      return set_result(text, anchor, 61, "Waterfall: frequency content history over time.", waterfall_rect);
    }

    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
    QRect time_rect(panel_x, panel_y, panel_w, panel_h);
    if (time_rect.contains(pos)) {
      return set_result(text, anchor, 62, "Time-Domain: I/Q waveform amplitude versus time.", time_rect);
    }

    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
    QRect const_rect(panel_x, panel_y, panel_w, panel_h);
    if (const_rect.contains(pos)) {
      return set_result(text, anchor, 63, "Constellation: I/Q point distribution and modulation stability.", const_rect);
    }
  }

  if (!in.ctrl.running_ui) {
    QRect simple_tab(lo.btn_tab_simple.x, lo.btn_tab_simple.y, lo.btn_tab_simple.w, lo.btn_tab_simple.h);
    if (simple_tab.contains(pos)) {
      return set_result(text, anchor, 20, "SIMPLE tab: quick setup with essential controls only.", simple_tab);
    }
    QRect detail_tab(lo.btn_tab_detail.x, lo.btn_tab_detail.y, lo.btn_tab_detail.w, lo.btn_tab_detail.h);
    if (detail_tab.contains(pos)) {
      return set_result(text, anchor, 21, "DETAIL tab: advanced RF, channel, and path controls.", detail_tab);
    }

    QRect start_btn(lo.btn_start.x, lo.btn_start.y, lo.btn_start.w, lo.btn_start.h);
    if (start_btn.contains(pos)) {
      if (!(in.ctrl.llh_ready || in.ctrl.interference_mode)) {
        return set_result(text, anchor, 22,
                          "START requires a valid LLH point, or enable INTERFERE mode.",
                          start_btn);
      }
      return set_result(text, anchor, 22,
                        "START: launch simulation with current control panel parameters.",
                        start_btn);
    }

    QRect exit_btn(lo.btn_exit.x, lo.btn_exit.y, lo.btn_exit.w, lo.btn_exit.h);
    if (exit_btn.contains(pos)) {
      return set_result(text, anchor, 23, "EXIT: request safe shutdown and close the app.", exit_btn);
    }
  }

  if (!in.ctrl.running_ui) {
    QRect sys_rect(lo.sw_sys.x, lo.sw_sys.y, lo.sw_sys.w, lo.sw_sys.h);
    if (sys_rect.contains(pos)) {
      return set_result(text, anchor, 24,
                        "SYSTEM: choose BDS / BDS+GPS / GPS. This also changes valid FS range.",
                        sys_rect);
    }

    QRect fs_rect(lo.fs_slider.x, lo.fs_slider.y, lo.fs_slider.w, lo.fs_slider.h);
    if (fs_rect.contains(pos)) {
      return set_result(text, anchor, 25,
                        "FS (Frequency): sample rate in MHz. Click/drag bar, or click value box to type.",
                        fs_rect);
    }
    QRect fs_val(slider_value_rect(lo.fs_slider).x, slider_value_rect(lo.fs_slider).y,
                 slider_value_rect(lo.fs_slider).w, slider_value_rect(lo.fs_slider).h);
    if (fs_val.contains(pos)) {
      return set_result(text, anchor, 26, "FS value box: click to enter an exact FS number.", fs_val);
    }

    if (in.ctrl.show_detailed_ctrl) {
      QRect gain_rect(lo.gain_slider.x, lo.gain_slider.y, lo.gain_slider.w, lo.gain_slider.h);
      if (gain_rect.contains(pos)) {
        return set_result(text, anchor, 27,
                          "Signal Gain: base signal amplitude scaling before TX stage.",
                          gain_rect);
      }
      QRect gain_val(slider_value_rect(lo.gain_slider).x, slider_value_rect(lo.gain_slider).y,
                     slider_value_rect(lo.gain_slider).w, slider_value_rect(lo.gain_slider).h);
      if (gain_val.contains(pos)) {
        return set_result(text, anchor, 28, "Signal Gain value box: click to type exact gain.", gain_val);
      }

      QRect cn0_rect(lo.cn0_slider.x, lo.cn0_slider.y, lo.cn0_slider.w, lo.cn0_slider.h);
      if (cn0_rect.contains(pos)) return set_result(text, anchor, 29, "Target C/N0: target signal quality level in dB-Hz.", cn0_rect);
      QRect cn0_val(slider_value_rect(lo.cn0_slider).x, slider_value_rect(lo.cn0_slider).y,
                    slider_value_rect(lo.cn0_slider).w, slider_value_rect(lo.cn0_slider).h);
      if (cn0_val.contains(pos)) return set_result(text, anchor, 30, "Target C/N0 value box: click to type exact C/N0.", cn0_val);

      QRect seed_rect(lo.seed_slider.x, lo.seed_slider.y, lo.seed_slider.w, lo.seed_slider.h);
      if (seed_rect.contains(pos)) return set_result(text, anchor, 31, "Seed: random seed for reproducible simulation output.", seed_rect);
      QRect seed_val(slider_value_rect(lo.seed_slider).x, slider_value_rect(lo.seed_slider).y,
                     slider_value_rect(lo.seed_slider).w, slider_value_rect(lo.seed_slider).h);
      if (seed_val.contains(pos)) return set_result(text, anchor, 32, "Seed value box: click to type exact seed number.", seed_val);

      QRect path_v_rect(lo.path_v_slider.x, lo.path_v_slider.y, lo.path_v_slider.w, lo.path_v_slider.h);
      if (path_v_rect.contains(pos)) return set_result(text, anchor, 33, "Path Vmax: route maximum speed limit.", path_v_rect);
      QRect path_v_val(slider_value_rect(lo.path_v_slider).x, slider_value_rect(lo.path_v_slider).y,
                       slider_value_rect(lo.path_v_slider).w, slider_value_rect(lo.path_v_slider).h);
      if (path_v_val.contains(pos)) return set_result(text, anchor, 34, "Path Vmax value box: click to type exact speed.", path_v_val);

      QRect path_a_rect(lo.path_a_slider.x, lo.path_a_slider.y, lo.path_a_slider.w, lo.path_a_slider.h);
      if (path_a_rect.contains(pos)) return set_result(text, anchor, 35, "Path Acc: route acceleration profile strength.", path_a_rect);
      QRect path_a_val(slider_value_rect(lo.path_a_slider).x, slider_value_rect(lo.path_a_slider).y,
                       slider_value_rect(lo.path_a_slider).w, slider_value_rect(lo.path_a_slider).h);
      if (path_a_val.contains(pos)) return set_result(text, anchor, 36, "Path Acc value box: click to type exact acceleration.", path_a_val);

      QRect prn_rect(lo.prn_slider.x, lo.prn_slider.y, lo.prn_slider.w, lo.prn_slider.h);
      if (prn_rect.contains(pos)) {
        return set_result(text, anchor, 37,
                          in.ctrl.sat_mode == 0
                              ? QString("PRN Select: choose a single satellite PRN.")
                              : QString("PRN Select is editable only in SINGLE mode."),
                          prn_rect);
      }
      QRect prn_val(slider_value_rect(lo.prn_slider).x, slider_value_rect(lo.prn_slider).y,
                    slider_value_rect(lo.prn_slider).w, slider_value_rect(lo.prn_slider).h);
      if (prn_val.contains(pos)) {
        return set_result(text, anchor, 38,
                          in.ctrl.sat_mode == 0
                              ? QString("PRN value box: click to type exact PRN ID.")
                              : QString("PRN value box disabled unless MODE is SINGLE."),
                          prn_val);
      }

      QRect ch_rect(lo.ch_slider.x, lo.ch_slider.y, lo.ch_slider.w, lo.ch_slider.h);
      if (ch_rect.contains(pos)) return set_result(text, anchor, 39, "Max CH: maximum simulated channel count (1-16).", ch_rect);
      QRect ch_val(slider_value_rect(lo.ch_slider).x, slider_value_rect(lo.ch_slider).y,
                   slider_value_rect(lo.ch_slider).w, slider_value_rect(lo.ch_slider).h);
      if (ch_val.contains(pos)) return set_result(text, anchor, 40, "Max CH value box: click to type exact channel count.", ch_val);

      QRect mode_rect(lo.sw_mode.x, lo.sw_mode.y, lo.sw_mode.w, lo.sw_mode.h);
      if (mode_rect.contains(pos)) {
        return set_result(text, anchor, 41, "MODE: choose satellite set behavior (SINGLE/range) by system type.", mode_rect);
      }

      QRect fmt_rect(lo.sw_fmt.x, lo.sw_fmt.y, lo.sw_fmt.w, lo.sw_fmt.h);
      if (fmt_rect.contains(pos)) {
        return set_result(text, anchor, 42, "FORMAT: choose output bit formatting (SHORT or BYTE).", fmt_rect);
      }

      QRect meo_rect(lo.tg_meo.x, lo.tg_meo.y, lo.tg_meo.w, lo.tg_meo.h);
      if (meo_rect.contains(pos)) return set_result(text, anchor, 43, "MEO: limit selection to MEO satellites only.", meo_rect);
      QRect iono_rect(lo.tg_iono.x, lo.tg_iono.y, lo.tg_iono.w, lo.tg_iono.h);
      if (iono_rect.contains(pos)) return set_result(text, anchor, 44, "IONO: enable/disable ionospheric effect model.", iono_rect);
      QRect clk_rect(lo.tg_clk.x, lo.tg_clk.y, lo.tg_clk.w, lo.tg_clk.h);
      if (clk_rect.contains(pos)) return set_result(text, anchor, 45, "EXT CLK: use external USRP reference clock.", clk_rect);
    } else {
      QRect tx_rect(lo.tx_slider.x, lo.tx_slider.y, lo.tx_slider.w, lo.tx_slider.h);
      if (tx_rect.contains(pos)) {
        return set_result(text, anchor, 46, "TX (Transmit Gain): RF transmit gain in dB.", tx_rect);
      }
      QRect tx_val(slider_value_rect(lo.tx_slider).x, slider_value_rect(lo.tx_slider).y,
                   slider_value_rect(lo.tx_slider).w, slider_value_rect(lo.tx_slider).h);
      if (tx_val.contains(pos)) {
        return set_result(text, anchor, 47, "TX value box: click to type exact transmit gain.", tx_val);
      }

      QRect jam_rect(lo.sw_jam.x, lo.sw_jam.y, lo.sw_jam.w, lo.sw_jam.h);
      if (jam_rect.contains(pos)) {
        return set_result(text, anchor, 48, "INTERFERE: choose SPOOF or JAM operation mode.", jam_rect);
      }
    }
  }

  QRect sat_map(in.win_width / 2, 0, in.win_width - in.win_width / 2, in.win_height / 2);
  if (sat_map.contains(pos)) {
    return set_result(text, anchor, 2,
                      "Satellite map: sky-view of visible satellites and their ground-referenced geometry.",
                      sat_map);
  }

  QRect osm = in.osm_panel_rect.adjusted(0, 0, -1, -1);
  if (osm.contains(pos)) {
    return set_result(text, anchor, 1,
                      in.ctrl.running_ui
                          ? QString("Map area: left-click for a straight-line path preview. Double-click for a road-based path preview. Right-click to confirm the path. Use the BACK button at the top to undo the last confirmed path segment that has not started yet. Use the search box at the upper left to find a location, then press RETURN to go back to the previous map center and zoom.")
                          : QString("Map area: use the search box at the upper left to find a location, or left-click to set the start LLH before pressing START."),
                      in.osm_panel_rect);
  }

  return -1;
}

void map_draw_hover_help_overlay(QPainter &p, const MapHoverHelpInput &in,
                                 bool hover_visible, const QString &hover_text,
                                 const QRect &hover_anchor) {
  if (!hover_visible || hover_text.isEmpty() || in.tutorial_overlay_visible) return;

  int box_w = std::max(320, std::min(560, in.win_width / 3));
  QFont old_font = p.font();
  QFont f = old_font;
  f.setPointSize(std::max(11, old_font.pointSize() + 1));
  p.setFont(f);
  QRect measure_rect(0, 0, box_w - 20, 2000);
  QRect text_bounds = p.fontMetrics().boundingRect(measure_rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                                                   hover_text);
  int box_h = std::max(62, std::min(180, text_bounds.height() + 20));
  int x = hover_anchor.center().x() - box_w / 2;
  int y = hover_anchor.y() - box_h - 10;

  if (x < 10) x = 10;
  if (x + box_w > in.win_width - 10) x = in.win_width - box_w - 10;
  if (y < 10) y = std::min(in.win_height - box_h - 10, hover_anchor.bottom() + 10);

  QRect bubble(x, y, box_w, box_h);
  if (!in.osm_stop_btn_rect.isEmpty() && bubble.intersects(in.osm_stop_btn_rect)) {
    int top_y = in.osm_stop_btn_rect.top() - box_h - 12;
    int bottom_y = in.osm_stop_btn_rect.bottom() + 12;
    if (top_y >= 10) {
      bubble.moveTop(top_y);
    } else if (bottom_y + box_h <= in.win_height - 10) {
      bubble.moveTop(bottom_y);
    } else {
      int left_x = in.osm_stop_btn_rect.left() - box_w - 12;
      int right_x = in.osm_stop_btn_rect.right() + 12;
      if (left_x >= 10) {
        bubble.moveLeft(left_x);
      } else if (right_x + box_w <= in.win_width - 10) {
        bubble.moveLeft(right_x);
      }
    }
    if (bubble.left() < 10) bubble.moveLeft(10);
    if (bubble.right() > in.win_width - 10) bubble.moveRight(in.win_width - 10);
    if (bubble.top() < 10) bubble.moveTop(10);
    if (bubble.bottom() > in.win_height - 10) bubble.moveBottom(in.win_height - 10);
  }
  if (!in.osm_runtime_rect.isEmpty() && bubble.intersects(in.osm_runtime_rect)) {
    int top_y = in.osm_runtime_rect.top() - box_h - 12;
    int bottom_y = in.osm_runtime_rect.bottom() + 12;
    if (top_y >= 10) {
      bubble.moveTop(top_y);
    } else if (bottom_y + box_h <= in.win_height - 10) {
      bubble.moveTop(bottom_y);
    } else {
      int left_x = in.osm_runtime_rect.left() - box_w - 12;
      int right_x = in.osm_runtime_rect.right() + 12;
      if (left_x >= 10) {
        bubble.moveLeft(left_x);
      } else if (right_x + box_w <= in.win_width - 10) {
        bubble.moveLeft(right_x);
      }
    }
    if (bubble.left() < 10) bubble.moveLeft(10);
    if (bubble.right() > in.win_width - 10) bubble.moveRight(in.win_width - 10);
    if (bubble.top() < 10) bubble.moveTop(10);
    if (bubble.bottom() > in.win_height - 10) bubble.moveBottom(in.win_height - 10);
  }
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor("#93c5fd"), 1));
  p.setBrush(QColor(8, 18, 34, 235));
  p.drawRoundedRect(bubble, 8, 8);

  p.setPen(QColor("#dbeafe"));
  p.drawText(bubble.adjusted(10, 8, -10, -8), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
             hover_text);
  p.setFont(old_font);
  p.setRenderHint(QPainter::Antialiasing, false);
}