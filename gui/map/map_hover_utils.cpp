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

QString hover_text(const MapHoverHelpInput &in, const char *en,
                   const char *zh_tw) {
  return gui_language_is_zh_tw(in.language) ? QString::fromUtf8(zh_tw)
                                             : QString::fromUtf8(en);
}

int clamp_int_local(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

} // namespace

int map_hover_region_for_pos(const QPoint &pos, const MapHoverHelpInput &in,
                             QString *text, QRect *anchor) {
  if (in.tutorial_overlay_visible) return -1;

  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.ctrl.show_detailed_ctrl);

  QRect utc_rect(lo.header_utc.x, lo.header_utc.y, lo.header_utc.w, lo.header_utc.h);
  if (utc_rect.contains(pos)) {
    return set_result(text, anchor, 6,
                      hover_text(in,
                                 "UTC: current coordinated universal time used for simulation timing.",
                                 "UTC：目前用於模擬時間基準的世界協調時間。"),
                      utc_rect);
  }

  auto right_rnx_rect = [](const Rect &row) {
    int right_w = clamp_int_local((int)std::lround((double)row.w * 0.44), 110,
                            std::max(110, row.w - 72));
    if (right_w > row.w - 36) right_w = std::max(24, row.w - 36);
    return QRect(row.x + row.w - right_w, row.y, right_w, row.h);
  };

  QRect bdt_row(lo.header_bdt.x, lo.header_bdt.y, lo.header_bdt.w, lo.header_bdt.h);
  QRect gpst_row(lo.header_gpst.x, lo.header_gpst.y, lo.header_gpst.w, lo.header_gpst.h);
  QRect bdt_rnx = right_rnx_rect(lo.header_bdt);
  QRect gpst_rnx = right_rnx_rect(lo.header_gpst);

  if ((bdt_row.contains(pos) && !bdt_rnx.contains(pos)) ||
      (gpst_row.contains(pos) && !gpst_rnx.contains(pos))) {
    return set_result(text, anchor, 7,
                      hover_text(in,
                                 "BDT/GPST: current BeiDou and GPS time references (week + seconds of week).",
                                 "BDT/GPST：目前北斗與 GPS 時間基準（週數與週內秒）。"),
                      bdt_row.united(gpst_row));
  }

  if (bdt_rnx.contains(pos) || gpst_rnx.contains(pos)) {
    return set_result(text, anchor, 8,
                      hover_text(in,
                                 "RNX: active RINEX navigation file name used by the simulator.",
                                 "RNX：模擬器目前使用的 RINEX 導航檔名稱。"),
                      bdt_rnx.united(gpst_rnx));
  }

  if (!in.dark_mode_btn_rect.isEmpty() && in.dark_mode_btn_rect.contains(pos)) {
    return set_result(text, anchor, 10,
                      in.dark_map_mode
                          ? hover_text(in,
                                       "LIGHT: switch map theme back to bright mode.",
                                       "亮色：將地圖主題切回明亮模式。")
                          : hover_text(in,
                                       "DARK: switch map theme to dark mode for low-light viewing.",
                                       "暗色：將地圖主題切為暗色，適合低光環境。"),
                      in.dark_mode_btn_rect);
  }
  if (!in.nfz_btn_rect.isEmpty() && in.nfz_btn_rect.contains(pos)) {
    return set_result(text, anchor, 11,
                      in.nfz_on
                          ? hover_text(in,
                                       "NFZ ON: DJI no-fly zones are visible. Click to hide them.",
                                       "NFZ 開：目前顯示 DJI 禁飛區。點擊可隱藏。")
                          : hover_text(in,
                                       "NFZ OFF: click to show DJI no-fly zones on the map.",
                                       "NFZ 關：點擊可在地圖上顯示 DJI 禁飛區。"),
                      in.nfz_btn_rect);
  }
  if (!in.tutorial_toggle_rect.isEmpty() && in.tutorial_toggle_rect.contains(pos)) {
    return set_result(text, anchor, 12,
                      in.ctrl.show_detailed_ctrl
                          ? hover_text(in,
                                       "GUIDE ON: guided overlay is enabled. Click to disable tutorial guidance.",
                                       "導覽開：教學覆蓋層已啟用。點擊可關閉教學導覽。")
                          : hover_text(in,
                                       "GUIDE OFF: click to enable step-by-step guided tutorial.",
                                       "導覽關：點擊可啟用逐步教學導覽。"),
                      in.tutorial_toggle_rect);
  }
  if (in.search_box_visible && in.search_box_rect.contains(pos)) {
    return set_result(text, anchor, 17,
                      hover_text(in,
                                 "Search box: enter place/address/lat,lon and press Enter to jump map center.",
                                 "搜尋框：輸入地點/地址/經緯度，按 Enter 可跳轉地圖中心。"),
                      in.search_box_rect);
  }
  if (in.show_search_return && !in.search_return_btn_rect.isEmpty() &&
      in.search_return_btn_rect.contains(pos)) {
    return set_result(text, anchor, 13,
                      hover_text(in,
                                 "RETURN: restore the map center and zoom from before the last search.",
                                 "返回：回到上一次搜尋前的地圖中心與縮放。"),
                      in.search_return_btn_rect);
  }
  if (in.ctrl.running_ui && !in.back_btn_rect.isEmpty() && in.back_btn_rect.contains(pos)) {
    return set_result(text, anchor, 14,
                      hover_text(in,
                                 "BACK: remove the last confirmed path segment that has not started yet.",
                                 "復原：移除最後一段已確認但尚未開始的路徑。"),
                      in.back_btn_rect);
  }
  if (in.ctrl.running_ui && !in.osm_stop_btn_rect.isEmpty() &&
      in.osm_stop_btn_rect.contains(pos)) {
    return set_result(text, anchor, 15,
                      hover_text(in,
                                 "STOP SIMULATION: end the active run and return to control setup.",
                                 "停止模擬：結束目前執行並回到控制設定。"),
                      in.osm_stop_btn_rect);
  }
  if (in.ctrl.running_ui && !in.osm_runtime_rect.isEmpty() &&
      in.osm_runtime_rect.contains(pos)) {
    return set_result(text, anchor, 16,
                      hover_text(in,
                                 "RUN TIME: elapsed time since initialization/transmit start; useful for timing checks.",
                                 "執行時間：自初始化/傳輸開始後的經過時間，可用於時間檢查。"),
                      in.osm_runtime_rect);
  }

  if (in.ctrl.running_ui) {
    int panel_x = 0, panel_y = 0, panel_w = 0, panel_h = 0;
    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
    QRect spectrum_rect(panel_x, panel_y, panel_w, panel_h);
    if (spectrum_rect.contains(pos)) {
      return set_result(text, anchor, 60,
                        hover_text(in,
                                   "Spectrum: power vs frequency (current frame).",
                                   "頻譜：目前畫面的頻率對功率分布。"),
                        spectrum_rect);
    }

    get_rb_lq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
    QRect waterfall_rect(panel_x, panel_y, panel_w, panel_h);
    if (waterfall_rect.contains(pos)) {
      return set_result(text, anchor, 61,
                        hover_text(in,
                                   "Waterfall: frequency content history over time.",
                                   "瀑布圖：頻率內容隨時間變化的歷史。"),
                        waterfall_rect);
    }

    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, false);
    QRect time_rect(panel_x, panel_y, panel_w, panel_h);
    if (time_rect.contains(pos)) {
      return set_result(text, anchor, 62,
                        hover_text(in,
                                   "Time-Domain: I/Q waveform amplitude versus time.",
                                   "時域：I/Q 波形振幅隨時間變化。"),
                        time_rect);
    }

    get_rb_rq_panel_rect(in.win_width, in.win_height, &panel_x, &panel_y, &panel_w, &panel_h, true);
    QRect const_rect(panel_x, panel_y, panel_w, panel_h);
    if (const_rect.contains(pos)) {
      return set_result(text, anchor, 63,
                        hover_text(in,
                                   "Constellation: I/Q point distribution and modulation stability.",
                                   "星座圖：I/Q 點分布與調變穩定度。"),
                        const_rect);
    }
  }

  if (!in.ctrl.running_ui) {
    QRect simple_tab(lo.btn_tab_simple.x, lo.btn_tab_simple.y, lo.btn_tab_simple.w, lo.btn_tab_simple.h);
    if (simple_tab.contains(pos)) {
      return set_result(text, anchor, 20,
                        hover_text(in,
                                   "SIMPLE tab: quick setup with essential controls only.",
                                   "簡易分頁：僅保留必要控制，快速完成設定。"),
                        simple_tab);
    }
    QRect detail_tab(lo.btn_tab_detail.x, lo.btn_tab_detail.y, lo.btn_tab_detail.w, lo.btn_tab_detail.h);
    if (detail_tab.contains(pos)) {
      return set_result(text, anchor, 21,
                        hover_text(in,
                                   "DETAIL tab: advanced RF, channel, and path controls.",
                                   "進階分頁：提供 RF、通道與路徑的進階控制。"),
                        detail_tab);
    }

    QRect start_btn(lo.btn_start.x, lo.btn_start.y, lo.btn_start.w, lo.btn_start.h);
    if (start_btn.contains(pos)) {
      if (!(in.ctrl.llh_ready || in.ctrl.interference_mode)) {
        return set_result(text, anchor, 22,
                          hover_text(in,
                                     "START requires a valid LLH point, or enable INTERFERE mode.",
                                     "START 需要有效 LLH 起點，或先啟用干擾模式。"),
                          start_btn);
      }
      return set_result(text, anchor, 22,
                        hover_text(in,
                                   "START: launch simulation with current control panel parameters.",
                                   "START：以目前控制面板參數啟動模擬。"),
                        start_btn);
    }

    QRect exit_btn(lo.btn_exit.x, lo.btn_exit.y, lo.btn_exit.w, lo.btn_exit.h);
    if (exit_btn.contains(pos)) {
      return set_result(text, anchor, 23,
                        hover_text(in,
                                   "EXIT: request safe shutdown and close the app.",
                                   "EXIT：安全結束並關閉程式。"),
                        exit_btn);
    }
  }

  if (!in.ctrl.running_ui) {
    QRect sys_rect(lo.sw_sys.x, lo.sw_sys.y, lo.sw_sys.w, lo.sw_sys.h);
    if (sys_rect.contains(pos)) {
      return set_result(text, anchor, 24,
                        hover_text(in,
                                   "SYSTEM: choose BDS / BDS+GPS / GPS. This also changes valid FS range.",
                                   "系統：選擇 BDS / BDS+GPS / GPS，會同步影響可用 FS 範圍。"),
                        sys_rect);
    }

    QRect fs_rect(lo.fs_slider.x, lo.fs_slider.y, lo.fs_slider.w, lo.fs_slider.h);
    if (fs_rect.contains(pos)) {
      return set_result(text, anchor, 25,
                        hover_text(in,
                                   "FS (Frequency): sample rate in MHz. Click/drag bar, or click value box to type.",
                                   "FS（頻率）：MHz 取樣率。可拖曳滑桿，或點值框直接輸入。"),
                        fs_rect);
    }
    QRect fs_val(slider_value_rect(lo.fs_slider).x, slider_value_rect(lo.fs_slider).y,
                 slider_value_rect(lo.fs_slider).w, slider_value_rect(lo.fs_slider).h);
    if (fs_val.contains(pos)) {
      return set_result(text, anchor, 26,
                        hover_text(in,
                                   "FS value box: click to enter an exact FS number.",
                                   "FS 數值框：點擊可輸入精確 FS。"),
                        fs_val);
    }

    if (in.ctrl.show_detailed_ctrl) {
      QRect gain_rect(lo.gain_slider.x, lo.gain_slider.y, lo.gain_slider.w, lo.gain_slider.h);
      if (gain_rect.contains(pos)) {
        return set_result(text, anchor, 27,
                          hover_text(in,
                                     "Signal Gain: base signal amplitude scaling before TX stage.",
                                     "訊號增益：TX 前的基礎訊號振幅縮放。"),
                          gain_rect);
      }
      QRect gain_val(slider_value_rect(lo.gain_slider).x, slider_value_rect(lo.gain_slider).y,
                     slider_value_rect(lo.gain_slider).w, slider_value_rect(lo.gain_slider).h);
      if (gain_val.contains(pos)) {
        return set_result(text, anchor, 28,
                          hover_text(in,
                                     "Signal Gain value box: click to type exact gain.",
                                     "訊號增益數值框：點擊可輸入精確增益。"),
                          gain_val);
      }

      QRect cn0_rect(lo.cn0_slider.x, lo.cn0_slider.y, lo.cn0_slider.w, lo.cn0_slider.h);
      if (cn0_rect.contains(pos))
        return set_result(text, anchor, 29,
                          hover_text(in,
                                     "Target C/N0: target signal quality level in dB-Hz.",
                                     "目標 C/N0：目標訊號品質（dB-Hz）。"),
                          cn0_rect);
      QRect cn0_val(slider_value_rect(lo.cn0_slider).x, slider_value_rect(lo.cn0_slider).y,
                    slider_value_rect(lo.cn0_slider).w, slider_value_rect(lo.cn0_slider).h);
      if (cn0_val.contains(pos))
        return set_result(text, anchor, 30,
                          hover_text(in,
                                     "Target C/N0 value box: click to type exact C/N0.",
                                     "目標 C/N0 數值框：點擊可輸入精確 C/N0。"),
                          cn0_val);

      QRect seed_rect(lo.seed_slider.x, lo.seed_slider.y, lo.seed_slider.w, lo.seed_slider.h);
      if (seed_rect.contains(pos))
        return set_result(text, anchor, 31,
                          hover_text(in,
                                     "Seed: random seed for reproducible simulation output.",
                                     "種子：讓模擬結果可重現的亂數種子。"),
                          seed_rect);
      QRect seed_val(slider_value_rect(lo.seed_slider).x, slider_value_rect(lo.seed_slider).y,
                     slider_value_rect(lo.seed_slider).w, slider_value_rect(lo.seed_slider).h);
      if (seed_val.contains(pos))
        return set_result(text, anchor, 32,
                          hover_text(in,
                                     "Seed value box: click to type exact seed number.",
                                     "種子數值框：點擊可輸入精確種子值。"),
                          seed_val);

      QRect path_v_rect(lo.path_v_slider.x, lo.path_v_slider.y, lo.path_v_slider.w, lo.path_v_slider.h);
      if (path_v_rect.contains(pos))
        return set_result(text, anchor, 33,
                          hover_text(in,
                                     "Path Vmax: route maximum speed limit.",
                                     "路徑最高速：路線允許的最大速度。"),
                          path_v_rect);
      QRect path_v_val(slider_value_rect(lo.path_v_slider).x, slider_value_rect(lo.path_v_slider).y,
                       slider_value_rect(lo.path_v_slider).w, slider_value_rect(lo.path_v_slider).h);
      if (path_v_val.contains(pos))
        return set_result(text, anchor, 34,
                          hover_text(in,
                                     "Path Vmax value box: click to type exact speed.",
                                     "路徑最高速數值框：點擊可輸入精確速度。"),
                          path_v_val);

      QRect path_a_rect(lo.path_a_slider.x, lo.path_a_slider.y, lo.path_a_slider.w, lo.path_a_slider.h);
      if (path_a_rect.contains(pos))
        return set_result(text, anchor, 35,
                          hover_text(in,
                                     "Path Acc: route acceleration profile strength.",
                                     "路徑加速度：路線加速曲線強度。"),
                          path_a_rect);
      QRect path_a_val(slider_value_rect(lo.path_a_slider).x, slider_value_rect(lo.path_a_slider).y,
                       slider_value_rect(lo.path_a_slider).w, slider_value_rect(lo.path_a_slider).h);
      if (path_a_val.contains(pos))
        return set_result(text, anchor, 36,
                          hover_text(in,
                                     "Path Acc value box: click to type exact acceleration.",
                                     "路徑加速度數值框：點擊可輸入精確加速度。"),
                          path_a_val);

      QRect prn_rect(lo.prn_slider.x, lo.prn_slider.y, lo.prn_slider.w, lo.prn_slider.h);
      if (prn_rect.contains(pos)) {
        return set_result(text, anchor, 37,
                          in.ctrl.sat_mode == 0
                              ? hover_text(in,
                                     "PRN Select: choose a single satellite PRN.",
                                     "PRN 選擇：選擇單一衛星 PRN。")
                              : hover_text(in,
                                     "PRN Select is editable only in SINGLE mode.",
                                     "PRN 僅在 SINGLE 模式可編輯。"),
                          prn_rect);
      }
      QRect prn_val(slider_value_rect(lo.prn_slider).x, slider_value_rect(lo.prn_slider).y,
                    slider_value_rect(lo.prn_slider).w, slider_value_rect(lo.prn_slider).h);
      if (prn_val.contains(pos)) {
        return set_result(text, anchor, 38,
                          in.ctrl.sat_mode == 0
                              ? hover_text(in,
                                     "PRN value box: click to type exact PRN ID.",
                                     "PRN 數值框：點擊可輸入精確 PRN 編號。")
                              : hover_text(in,
                                     "PRN value box disabled unless MODE is SINGLE.",
                                     "除非 MODE 為 SINGLE，否則 PRN 數值框不可用。"),
                          prn_val);
      }

      QRect ch_rect(lo.ch_slider.x, lo.ch_slider.y, lo.ch_slider.w, lo.ch_slider.h);
      if (ch_rect.contains(pos))
        return set_result(text, anchor, 39,
                          hover_text(in,
                                     "Max CH: maximum simulated channel count (1-16).",
                                     "最大通道：模擬通道上限（1-16）。"),
                          ch_rect);
      QRect ch_val(slider_value_rect(lo.ch_slider).x, slider_value_rect(lo.ch_slider).y,
                   slider_value_rect(lo.ch_slider).w, slider_value_rect(lo.ch_slider).h);
      if (ch_val.contains(pos))
        return set_result(text, anchor, 40,
                          hover_text(in,
                                     "Max CH value box: click to type exact channel count.",
                                     "最大通道數值框：點擊可輸入精確通道數。"),
                          ch_val);

      QRect mode_rect(lo.sw_mode.x, lo.sw_mode.y, lo.sw_mode.w, lo.sw_mode.h);
      if (mode_rect.contains(pos)) {
        return set_result(text, anchor, 41,
                          hover_text(in,
                                     "MODE: choose satellite set behavior (SINGLE/range) by system type.",
                                     "MODE：依系統類型選擇衛星集行為（SINGLE/範圍）。"),
                          mode_rect);
      }

      QRect fmt_rect(lo.sw_fmt.x, lo.sw_fmt.y, lo.sw_fmt.w, lo.sw_fmt.h);
      if (fmt_rect.contains(pos)) {
        return set_result(text, anchor, 42,
                          hover_text(in,
                                     "FORMAT: choose output bit formatting (SHORT or BYTE).",
                                     "FORMAT：選擇輸出格式（SHORT 或 BYTE）。"),
                          fmt_rect);
      }

      QRect meo_rect(lo.tg_meo.x, lo.tg_meo.y, lo.tg_meo.w, lo.tg_meo.h);
      if (meo_rect.contains(pos))
        return set_result(text, anchor, 43,
                          hover_text(in,
                                     "MEO: limit selection to MEO satellites only.",
                                     "MEO：限制只使用 MEO 衛星。"),
                          meo_rect);
      QRect iono_rect(lo.tg_iono.x, lo.tg_iono.y, lo.tg_iono.w, lo.tg_iono.h);
      if (iono_rect.contains(pos))
        return set_result(text, anchor, 44,
                          hover_text(in,
                                     "IONO: enable/disable ionospheric effect model.",
                                     "IONO：開啟/關閉電離層效應模型。"),
                          iono_rect);
      QRect clk_rect(lo.tg_clk.x, lo.tg_clk.y, lo.tg_clk.w, lo.tg_clk.h);
      if (clk_rect.contains(pos))
        return set_result(text, anchor, 45,
                          hover_text(in,
                                     "EXT CLK: use external USRP reference clock.",
                                     "EXT CLK：使用外部 USRP 參考時鐘。"),
                          clk_rect);
    } else {
      QRect tx_rect(lo.tx_slider.x, lo.tx_slider.y, lo.tx_slider.w, lo.tx_slider.h);
      if (tx_rect.contains(pos)) {
        return set_result(text, anchor, 46,
                          hover_text(in,
                                     "TX (Transmit Gain): RF transmit gain in dB.",
                                     "TX（發射增益）：RF 發射增益（dB）。"),
                          tx_rect);
      }
      QRect tx_val(slider_value_rect(lo.tx_slider).x, slider_value_rect(lo.tx_slider).y,
                   slider_value_rect(lo.tx_slider).w, slider_value_rect(lo.tx_slider).h);
      if (tx_val.contains(pos)) {
        return set_result(text, anchor, 47,
                          hover_text(in,
                                     "TX value box: click to type exact transmit gain.",
                                     "TX 數值框：點擊可輸入精確發射增益。"),
                          tx_val);
      }

      QRect jam_rect(lo.sw_jam.x, lo.sw_jam.y, lo.sw_jam.w, lo.sw_jam.h);
      if (jam_rect.contains(pos)) {
        return set_result(text, anchor, 48,
                          hover_text(in,
                                     "INTERFERE: choose SPOOF or JAM operation mode.",
                                     "干擾：選擇 SPOOF 或 JAM 作業模式。"),
                          jam_rect);
      }
    }
  }

  QRect sat_map(in.win_width / 2, 0, in.win_width - in.win_width / 2, in.win_height / 2);
  if (sat_map.contains(pos)) {
    return set_result(text, anchor, 2,
                      hover_text(in,
                                 "Satellite map: sky-view of visible satellites and their ground-referenced geometry.",
                                 "衛星地圖：顯示可見衛星的天空視圖與地面參考幾何。"),
                      sat_map);
  }

  QRect osm = in.osm_panel_rect.adjusted(0, 0, -1, -1);
  if (osm.contains(pos)) {
    return set_result(text, anchor, 1,
                      in.ctrl.running_ui
                          ? hover_text(in,
                                       "Map area: left-click for a straight-line path preview. Double-click for a road-based path preview. Right-click to confirm the path. Use the BACK button at the top to undo the last confirmed path segment that has not started yet. Use the search box at the upper left to find a location, then press RETURN to go back to the previous map center and zoom.",
                                       "地圖區域：左鍵預覽直線路徑，雙擊預覽道路路徑，右鍵確認路徑。可用上方 BACK 復原最後一段尚未開始的已確認路徑。也可用左上搜尋框找地點，再按 RETURN 回到先前地圖中心與縮放。")
                          : hover_text(in,
                                       "Map area: use the search box at the upper left to find a location, or left-click to set the start LLH before pressing START.",
                                       "地圖區域：可用左上搜尋框找地點，或先左鍵設定起始 LLH，再按 START。"),
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