#include "gui/layout/map_control_panel_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"
#include "gui/core/rf_mode_utils.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
#include "bdssim.h"
}

namespace {

QString short_base_name(const QString &path) {
  int slash_pos = std::max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  return (slash_pos >= 0) ? path.mid(slash_pos + 1) : path;
}

} // namespace

void map_draw_control_panel(QPainter &p, int win_width, int win_height,
                            const MapControlPanelInput &in) {
  const GuiControlState &st = in.ctrl;

  QColor color_border("#b9cadf");
  QColor color_text("#f8fbff");
  QColor color_dim("#6b7b90");
  QColor color_panel_top("#0e1f34");
  QColor color_panel_bottom("#06111f");
  QColor color_title("#00e5ff");
  QColor color_start_btn("#1fbe7b");
  QColor color_stop_btn("#ef5350");
  QColor color_start_off("#275844");
  QColor color_exit_off("#6b3a3a");
  QColor color_btn_text_black(8, 12, 18);

  ControlLayout lo;
  compute_control_layout(win_width, win_height, &lo, st.show_detailed_ctrl);

  QRect panel_rect(lo.panel.x, lo.panel.y, lo.panel.w - 1, lo.panel.h - 1);
  QLinearGradient panel_grad(panel_rect.topLeft(), panel_rect.bottomLeft());
  panel_grad.setColorAt(0.0, color_panel_top);
  panel_grad.setColorAt(1.0, color_panel_bottom);

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(panel_grad);
  p.drawRoundedRect(panel_rect, 10, 10);
  p.setRenderHint(QPainter::Antialiasing, false);

  auto draw_section_frame = [&](const QRect &r) {
    if (r.width() <= 8 || r.height() <= 8) return;
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(186, 224, 255, 220), 2));
    p.setBrush(QColor(24, 56, 88, 120));
    p.drawRoundedRect(r, 8, 8);
    p.setPen(QPen(QColor(232, 243, 255, 190), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 7, 7);
    p.setRenderHint(QPainter::Antialiasing, false);
  };

  int aligned_left = lo.btn_start.x;
  int aligned_right = lo.btn_exit.x + lo.btn_exit.w;
  int frame_pad_x = 6;
  int frame_pad_y = 6;
  int section_x = std::max(lo.panel.x + 4, aligned_left - frame_pad_x);
  int section_right = std::min(lo.panel.x + lo.panel.w - 4, aligned_right + frame_pad_x);
  int section_w = std::max(0, section_right - section_x);
  int action_top = lo.btn_start.y - frame_pad_y;
  int action_bottom = lo.btn_start.y + lo.btn_start.h + frame_pad_y;
  draw_section_frame(QRect(section_x, action_top, section_w,
                           std::max(0, action_bottom - action_top)));

  bool sys_ctrl_enabled = !st.running_ui;
  bool mode_jam = (st.interference_selection == 1);
  bool mode_spoof = (st.interference_selection == 0) && st.spoof_allowed;
  bool mode_any = (mode_jam || mode_spoof);
  bool jam_ctrl_enabled = !st.running_ui;
  bool simple_fs_enabled = mode_spoof && !st.running_ui;
  bool simple_tx_enabled = mode_any && !st.running_ui;
  bool detail_gain_enabled = mode_any && !st.running_ui;
  bool detail_cn0_enabled = mode_any && !st.running_ui;
  bool detail_fmt_enabled = mode_any && !st.running_ui;
  bool detail_mode_enabled = mode_spoof && !st.running_ui;
  bool detail_other_enabled = mode_spoof && !st.running_ui;
  bool start_enabled = !st.running_ui && mode_any;

  QFont base_font = p.font();
  base_font.setPointSize(clamp_int(lo.panel.h / 44, 8, 16));

  QFont title_font = base_font;
  title_font.setBold(true);
  title_font.setPointSize(clamp_int(title_font.pointSize() + 2, 10, 18));
  p.setFont(title_font);
  p.setPen(color_title);
  p.drawText(QRect(lo.header_title.x, lo.header_title.y, lo.header_title.w, lo.header_title.h),
             Qt::AlignVCenter | Qt::AlignLeft, "Signal Settings");

  QFont utc_font = base_font;
  utc_font.setFamily("Monospace");
  utc_font.setPointSize(clamp_int(base_font.pointSize() + lo.panel.h / 180, 8, 14));
  utc_font.setBold(true);
  p.setFont(utc_font);
  p.setPen(QColor("#00ffcc"));
  p.drawText(QRect(lo.header_utc.x, lo.header_utc.y, lo.header_utc.w, lo.header_utc.h),
             Qt::AlignVCenter | Qt::AlignRight, in.time_info.utc_label);

  QFont mono_font = base_font;
  mono_font.setFamily("Monospace");
  mono_font.setPointSize(clamp_int(base_font.pointSize(), 8, 12));
  p.setFont(mono_font);

  QString rnx_name = short_base_name(in.rnx_name.isEmpty() ? QString("N/A") : in.rnx_name);
  QString bdt_str = QString("Week %1 SOW %2").arg(in.time_info.bdt_week).arg(in.time_info.bdt_sow, 0, 'f', 1);
  QString gpst_str = QString("Week %1 SOW %2").arg(in.time_info.gpst_week).arg(in.time_info.gpst_sow, 0, 'f', 1);

  auto draw_time_rnx_row = [&](const Rect &row, const QString &left_text) {
    int right_w = clamp_int((int)std::lround((double)row.w * 0.44), 110,
                            std::max(110, row.w - 72));
    if (right_w > row.w - 36) right_w = std::max(24, row.w - 36);
    int left_w = std::max(24, row.w - right_w - 8);
    QRect left_rect(row.x, row.y, left_w, row.h);
    QRect right_rect(row.x + row.w - right_w, row.y, right_w, row.h);
    QFontMetrics fm(p.font());
    QString right_text = QString("RNX | %1").arg(rnx_name);

    p.setPen(QColor("#9bd2ff"));
    p.drawText(left_rect, Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(left_text, Qt::ElideRight, left_rect.width()));

    p.setPen(QColor("#b7c8dc"));
    p.drawText(right_rect, Qt::AlignVCenter | Qt::AlignRight,
               fm.elidedText(right_text, Qt::ElideLeft, right_rect.width()));
  };

  draw_time_rnx_row(lo.header_bdt, QString("BDT  | %1").arg(bdt_str));
  draw_time_rnx_row(lo.header_gpst, QString("GPST | %1").arg(gpst_str));

  QString cand_line;
  if (st.single_candidate_count <= 0) {
    cand_line = "SATS | none";
  } else {
    cand_line = "SATS |";
    bool overflow = false;
    for (int i = 0; i < st.single_candidate_count; ++i) {
      QString trial = cand_line + QString(" %1").arg(st.single_candidates[i]);
      if (p.fontMetrics().horizontalAdvance(trial) > lo.detail_sats.w - 20) {
        overflow = true;
        break;
      }
      cand_line = trial;
    }
    if (overflow) {
      cand_line += QString(" ... (%1)").arg(st.single_candidate_count);
    }
  }

  p.setFont(base_font);

  auto draw_tab_btn = [&](Rect r, const QString &text, bool active) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    int radius = 10;
    path.moveTo(r.x, r.y + r.h);
    path.lineTo(r.x, r.y + radius);
    path.arcTo(r.x, r.y, radius * 2, radius * 2, 180.0, -90.0);
    path.lineTo(r.x + r.w - radius, r.y);
    path.arcTo(r.x + r.w - radius * 2, r.y, radius * 2, radius * 2, 90.0, -90.0);
    path.lineTo(r.x + r.w, r.y + r.h);
    path.closeSubpath();

    QColor bg = active ? QColor("#1e3a5f") : QColor(10, 20, 35, 100);
    QColor text_col = active ? QColor("#00ffcc") : QColor("#6b7b90");
    p.fillPath(path, bg);
    if (active) {
      p.setPen(QPen(bg, 4));
      p.drawLine(r.x + 1, r.y + r.h, r.x + r.w - 1, r.y + r.h);
    }
    p.setPen(text_col);
    QFont f = p.font();
    f.setBold(active);
    f.setPointSize(clamp_int(std::max(9, r.h / 2), 9, 14));
    p.setFont(f);
    p.drawText(QRect(r.x, r.y, r.w, r.h), Qt::AlignCenter, text);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(base_font);
  };

  p.setPen(QPen(QColor("#4b5b70"), 2));
  p.drawLine(lo.btn_tab_simple.x, lo.btn_tab_simple.y + lo.btn_tab_simple.h,
             lo.btn_tab_detail.x + lo.btn_tab_detail.w, lo.btn_tab_simple.y + lo.btn_tab_simple.h);
  draw_tab_btn(lo.btn_tab_simple, "SIMPLE", !st.show_detailed_ctrl);
  draw_tab_btn(lo.btn_tab_detail, "DETAIL", st.show_detailed_ctrl);

  char v_tx[32], v_gain[32], v_fs[32], v_cn0[32], v_seed[32], v_prn[64],
      v_path_v[32], v_path_a[32], v_ch[32];
  std::snprintf(v_tx, sizeof(v_tx), "%.1f dB", st.tx_gain);
  std::snprintf(v_gain, sizeof(v_gain), "%.2f", st.gain);
  std::snprintf(v_fs, sizeof(v_fs), "%.1f MHz", st.fs_mhz);
  std::snprintf(v_cn0, sizeof(v_cn0), "%.1f dB-Hz", st.target_cn0);
  std::snprintf(v_seed, sizeof(v_seed), "%u", st.seed);
  std::snprintf(v_path_v, sizeof(v_path_v), "%.1f km/h", st.path_vmax_kmh);
  std::snprintf(v_path_a, sizeof(v_path_a), "%.1f m/s2", st.path_accel_mps2);
  std::snprintf(v_ch, sizeof(v_ch), "%d", st.max_ch);

  if (st.sat_mode == 0) {
    std::snprintf(v_prn, sizeof(v_prn), "PRN %d", st.single_prn);
  } else {
    std::snprintf(v_prn, sizeof(v_prn), "N/A");
  }

  double fs_ratio = (st.fs_mhz - 2.6) / std::max(0.1, 31.2 - 2.6);
  int max_prn = (st.signal_mode == SIG_MODE_GPS) ? 32 : 63;
  double prn_ratio = (st.single_prn >= 1) ? ((double)st.single_prn - 1.0) / (max_prn - 1.0) : 0.0;
  double seed_ratio = ((double)st.seed - 1.0) / 9.0;
  double min_v = 3.6, max_v = 2000.0;
  double path_v_ratio = std::log(std::max(min_v, st.path_vmax_kmh) / min_v) / std::log(max_v / min_v);
  double min_a = 0.2, max_a = 100.0;
  double path_a_ratio = std::log(std::max(min_a, st.path_accel_mps2) / min_a) / std::log(max_a / min_a);
  int sys_idx = (st.signal_mode == SIG_MODE_BDS) ? 0 : ((st.signal_mode == SIG_MODE_MIXED) ? 1 : 2);

  if (!st.show_detailed_ctrl) {
    control_draw_two_switch(p, lo.sw_jam, color_border, color_text, color_dim,
                            QColor(239, 68, 68, 220), "INTERFERE", "SPOOF",
                            "JAM", st.interference_selection, jam_ctrl_enabled);
  }

  control_draw_three_switch(p, lo.sw_sys, color_border, color_text, color_dim,
                            QColor(24, 160, 126, 220), "SYSTEM", "BDS",
                            "BDS+GPS", "GPS", sys_idx, sys_ctrl_enabled);

  control_draw_slider(p, lo.fs_slider, color_border, color_text, color_dim,
                      QColor(59, 130, 246, 220), "FS (Frequency)", v_fs,
                      fs_ratio, simple_fs_enabled);

  if (st.show_detailed_ctrl) {
    control_draw_slider(p, lo.gain_slider, color_border, color_text, color_dim,
                        QColor(96, 165, 250, 220), "Signal Gain", v_gain,
                        (st.gain - 0.1) / (20.0 - 0.1), detail_gain_enabled);
  }
  if (!st.show_detailed_ctrl) {
    control_draw_slider(p, lo.tx_slider, color_border, color_text, color_dim,
                        QColor(56, 189, 248, 220), "TX (Transmit Gain)", v_tx,
                        st.tx_gain / 100.0, simple_tx_enabled);
  }

  if (st.show_detailed_ctrl) {
    if (lo.detail_sats.h > 0) {
      QFont detail_info_font = base_font;
      detail_info_font.setFamily("Monospace");
      p.setFont(detail_info_font);
      p.setPen(mode_any ? QColor("#b9cadf") : color_dim);
      p.drawText(QRect(lo.detail_sats.x, lo.detail_sats.y, lo.detail_sats.w, lo.detail_sats.h),
                 Qt::AlignVCenter | Qt::AlignLeft,
                 p.fontMetrics().elidedText(cand_line, Qt::ElideRight, lo.detail_sats.w));
      p.setFont(base_font);
    }

    control_draw_slider(p, lo.cn0_slider, color_border, color_text, color_dim,
                        QColor(14, 165, 233, 220), "Target C/N0", v_cn0,
                        (st.target_cn0 - 20.0) / 40.0, detail_cn0_enabled);
    control_draw_slider(p, lo.seed_slider, color_border, color_text, color_dim,
                        QColor(52, 211, 153, 220), "Seed", v_seed,
                        seed_ratio, detail_other_enabled);
    control_draw_slider(p, lo.prn_slider, color_border, color_text, color_dim,
                        QColor(45, 212, 191, 220), "PRN Select", v_prn,
                        prn_ratio, detail_other_enabled && st.sat_mode == 0);
    control_draw_slider(p, lo.path_v_slider, color_border, color_text, color_dim,
                        QColor(248, 113, 113, 220), "Path Vmax", v_path_v,
                        path_v_ratio, detail_other_enabled);
    control_draw_slider(p, lo.path_a_slider, color_border, color_text, color_dim,
                        QColor(251, 146, 60, 220), "Path Acc", v_path_a,
                        path_a_ratio, detail_other_enabled);
    control_draw_slider(p, lo.ch_slider, color_border, color_text, color_dim,
                        QColor(250, 204, 21, 220), "Max CH", v_ch,
                        ((double)st.max_ch - 1.0) / 15.0, detail_other_enabled);

    if (st.signal_mode == SIG_MODE_BDS) {
      control_draw_three_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                                QColor(38, 115, 219, 220), "MODE", "SINGLE",
                                "1-37", "1-63", st.sat_mode, detail_mode_enabled);
    } else if (st.signal_mode == SIG_MODE_MIXED) {
      int active_idx = (st.sat_mode == 1) ? 0 : 1;
      control_draw_two_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                              QColor(38, 115, 219, 220), "MODE", "BDS-37+GPS",
                              "BDS-63+GPS", active_idx, detail_mode_enabled);
    } else {
      int active_idx = (st.sat_mode == 0) ? 0 : 1;
      control_draw_two_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                              QColor(38, 115, 219, 220), "MODE", "SINGLE",
                              "1-32", active_idx, detail_mode_enabled);
    }

    control_draw_checkbox(p, lo.tg_meo, color_border, color_text, color_dim,
                          "MEO", st.meo_only, detail_other_enabled);
    control_draw_checkbox(p, lo.tg_iono, color_border, color_text, color_dim,
                          "IONO", st.iono_on, detail_other_enabled);
    control_draw_checkbox(p, lo.tg_clk, color_border, color_text, color_dim,
                          "EXT CLK", st.usrp_external_clk, detail_other_enabled);

    control_draw_two_switch(p, lo.sw_fmt, color_border, color_text, color_dim,
                            QColor(56, 189, 248, 220), "FORMAT", "SHORT",
                            "BYTE", st.byte_output ? 1 : 0, detail_fmt_enabled);
  }

  if (lo.content_frame.w > 0 && lo.content_frame.h > 0) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(139, 195, 255, 180), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRect(lo.content_frame.x, lo.content_frame.y,
                            lo.content_frame.w, lo.content_frame.h),
                      8, 8);
    p.setRenderHint(QPainter::Antialiasing, false);
  }

  if (lo.btn_start.w > 0) {
    control_draw_button_filled(p, lo.btn_start,
                               start_enabled ? color_start_btn : color_start_off,
                               start_enabled ? color_start_btn : color_start_off,
                               color_btn_text_black, "START");
  }

  if (lo.btn_exit.w > 0) {
    bool exit_enabled = !st.running_ui;
    control_draw_button_filled(p, lo.btn_exit,
                               exit_enabled ? color_stop_btn : color_exit_off,
                               exit_enabled ? color_stop_btn : color_exit_off,
                               color_btn_text_black, "EXIT");
  }
}