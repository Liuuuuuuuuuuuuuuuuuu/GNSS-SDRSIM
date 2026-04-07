#include "gui/layout/map_control_panel_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"
#include "gui/core/gui_font_manager.h"
#include "gui/core/gui_i18n.h"
#include "gui/core/rf_mode_utils.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPen>
#include <QRegularExpression>

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

QString compact_rnx_suffix_text(const QString &name) {
  QString base = short_base_name(name).trimmed();
  if (base.isEmpty()) return QString("N/A");

  QString lower = base.toLower();
  int rnx_pos = lower.lastIndexOf(".rnx");
  QString ext = (rnx_pos >= 0) ? base.mid(rnx_pos) : QString(".rnx");

  QRegularExpression re("(\\d{7,})");
  QRegularExpressionMatchIterator it = re.globalMatch(base);
  QString best;
  while (it.hasNext()) {
    QRegularExpressionMatch m = it.next();
    QString token = m.captured(1);
    if (token.size() > best.size()) best = token;
  }

  if (!best.isEmpty()) {
    QString core = (best.size() > 11) ? best.right(11) : best;
    return QString("...%1...%2").arg(core, ext.toLower());
  }

  if (base.size() <= 16) return base;
  return QString("...%1").arg(base.right(13));
}

QString week_sow_compact(int week, double sow) {
  const double clamped_sow = std::max(0.0, sow);
  const long long sow_tenths = (long long)std::llround(clamped_sow * 10.0);
  const long long sow_int = sow_tenths / 10;
  const int sow_frac = (int)(sow_tenths % 10);
  const QString sow_text = QString("%1.%2")
                               .arg(sow_int, 5, 10, QChar('0'))
                               .arg(sow_frac);
  return QString("W%1 SOW %2")
      .arg(week, 4, 10, QChar('0'))
      .arg(sow_text);
}

} // namespace

void map_draw_control_panel(QPainter &p, int win_width, int win_height,
                            const MapControlPanelInput &in) {
  const GuiControlState &st = in.ctrl;

  QColor color_border = in.border_color.isValid() ? in.border_color : QColor("#b9cadf");
  QColor color_text = in.text_color.isValid() ? in.text_color : QColor("#f8fbff");
  QColor color_dim = in.dim_text_color.isValid() ? in.dim_text_color : QColor("#6b7b90");
  QColor color_panel_top("#0e1f34");
  QColor color_panel_bottom("#06111f");
  QColor color_title = in.accent_color.isValid() ? in.accent_color : QColor("#00e5ff");
  QColor color_start_btn("#1fbe7b");
  QColor color_stop_btn("#ef5350");
  QColor color_start_off("#275844");
  QColor color_exit_off("#6b3a3a");
  QColor color_btn_text_black(8, 12, 18);

  ControlLayout lo;
  compute_control_layout(win_width, win_height, &lo, st.show_detailed_ctrl);

  double panel_scale_w = clamp_double((double)lo.panel.w / 520.0, 0.78, 1.12);
  double panel_scale_h = clamp_double((double)lo.panel.h / 330.0, 0.76, 1.12);
  double auto_scale = std::min(panel_scale_w, panel_scale_h);
  const bool portrait_template = lo.panel.h > lo.panel.w;
  if (portrait_template) {
    auto_scale *= st.show_detailed_ctrl ? 0.95 : 0.98;
  } else if (lo.panel.w >= lo.panel.h * 1.85) {
    auto_scale *= 1.03;
  }
  if (st.show_detailed_ctrl && lo.panel.w < 440) {
    auto_scale *= 0.97;
  }

  // Keep one shared auto-scale; the per-category ratios come from style dialog
  // (Master/Caption/Switch/Value) so users can reason about exact percentages.
  control_paint_set_detail_scales(in.control_text_scale * auto_scale,
                                  in.caption_text_scale * auto_scale,
                                  in.switch_option_text_scale * auto_scale,
                                  in.value_text_scale * auto_scale);

  QRect panel_rect(lo.panel.x, lo.panel.y, lo.panel.w - 1, lo.panel.h - 1);
  QLinearGradient panel_grad(panel_rect.topLeft(), panel_rect.bottomLeft());
  panel_grad.setColorAt(0.0, color_panel_top);
  panel_grad.setColorAt(1.0, color_panel_bottom);

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(Qt::NoPen);
  p.setBrush(panel_grad);
  p.drawRoundedRect(panel_rect, 10, 10);
  // Static white outer frame (consistent with Skyplot panel style)
  p.setPen(QPen(QColor(255, 255, 255, 200), 2.0));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(panel_rect.adjusted(-2, -2, 2, 2), 12, 12);
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
  bool detail_other_enabled = mode_any && !st.running_ui;
  bool start_enabled = !st.running_ui && mode_any;

  QFont base_font = p.font();
  base_font.setPointSize(
      clamp_int((int)std::lround((double)(lo.panel.h / 38) * auto_scale), 9, 19));

  QFont title_font = base_font;
  title_font.setBold(true);
  title_font.setPointSize(clamp_int(title_font.pointSize() + 3, 11, 22));
  p.setFont(title_font);
  p.setPen(color_title);
  QString header_title_text = portrait_template ? QString("Setting")
                                                : gui_i18n_text(in.language, "panel.signal_settings");
  p.drawText(QRect(lo.header_title.x, lo.header_title.y, lo.header_title.w, lo.header_title.h),
             Qt::AlignVCenter | Qt::AlignLeft,
             header_title_text);

  auto draw_gear_button = [&](const Rect &r) {
    QRect gr(r.x, r.y, r.w, r.h);
    if (gr.width() <= 4 || gr.height() <= 4) return;

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(186, 224, 255, 220), 1));
    p.setBrush(QColor(22, 38, 56, 210));
    p.drawRoundedRect(gr, 4, 4);

    QPointF c = gr.center();
    double radius = (double)std::min(gr.width(), gr.height()) * 0.26;
    p.setPen(QPen(color_title, 1.6));
    for (int i = 0; i < 8; ++i) {
      const double pi = 3.14159265358979323846;
      double a = (pi * 2.0 * i) / 8.0;
      QPointF p0(c.x() + std::cos(a) * (radius + 1.0),
                 c.y() + std::sin(a) * (radius + 1.0));
      QPointF p1(c.x() + std::cos(a) * (radius + 4.0),
                 c.y() + std::sin(a) * (radius + 4.0));
      p.drawLine(p0, p1);
    }
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, radius + 1.0, radius + 1.0);
    p.setBrush(color_title);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, 2.0, 2.0);
    p.setRenderHint(QPainter::Antialiasing, false);
  };
  draw_gear_button(lo.header_gear);

  QFont utc_font = gui_font_mono(
      clamp_int(base_font.pointSize() + lo.panel.h / 170 + 1, 9, 16));
  utc_font.setBold(true);
  while (utc_font.pointSize() > 8) {
    QFontMetrics fm(utc_font);
    if (fm.horizontalAdvance(in.time_info.utc_label) <= lo.header_utc.w - 2) break;
    utc_font.setPointSize(utc_font.pointSize() - 1);
  }
  p.setFont(utc_font);
  p.setPen(QColor("#00ffcc"));
  p.drawText(QRect(lo.header_utc.x, lo.header_utc.y, lo.header_utc.w, lo.header_utc.h),
             Qt::AlignVCenter | Qt::AlignRight, in.time_info.utc_label);

  QFont mono_font = gui_font_mono(clamp_int(base_font.pointSize(), 9, 14));
  p.setFont(mono_font);

  QString rnx_name_full = short_base_name(in.rnx_name.isEmpty() ? QString("N/A") : in.rnx_name);
  QString rnx_name_compact = compact_rnx_suffix_text(in.rnx_name.isEmpty() ? QString("N/A") : in.rnx_name);
  QString bdt_str = week_sow_compact(in.time_info.bdt_week, in.time_info.bdt_sow);
  QString gpst_str = week_sow_compact(in.time_info.gpst_week, in.time_info.gpst_sow);

  auto draw_time_rnx_row = [&](const Rect &row, const QString &sys_label,
                               const QString &week_sow_text) {
    int right_w = clamp_int((int)std::lround((double)row.w * 0.46), 120,
                            std::max(120, row.w - 72));
    if (right_w > row.w - 36) right_w = std::max(24, row.w - 36);
    int left_w = std::max(24, row.w - right_w - 8);
    QRect left_rect(row.x, row.y, left_w, row.h);
    QRect right_rect(row.x + row.w - right_w, row.y, right_w, row.h);
    QFontMetrics fm(p.font());
    QString aligned_label = sys_label.leftJustified(4, QChar(' '));
    QString left_text = QString("%1 | %2").arg(aligned_label, week_sow_text);
    QString right_text_full = QString("RNX: %1").arg(rnx_name_full);
    QString right_text_compact = QString("RNX: %1").arg(rnx_name_compact);
    bool show_rnx = true;
    if (portrait_template) {
      show_rnx = (fm.horizontalAdvance(right_text_compact) <= right_rect.width());
      if (!show_rnx) {
        left_rect = QRect(row.x, row.y, row.w, row.h);
      }
    }

    p.setPen(QColor("#9bd2ff"));
    p.drawText(left_rect, Qt::AlignVCenter | Qt::AlignLeft,
               fm.elidedText(left_text, Qt::ElideRight, left_rect.width()));

    if (show_rnx) {
      p.setPen(QColor("#b7c8dc"));
      QString right_text = right_text_compact;
      if (!portrait_template && fm.horizontalAdvance(right_text_full) <= right_rect.width()) {
        right_text = right_text_full;
      }
      p.drawText(right_rect, Qt::AlignVCenter | Qt::AlignRight,
                 portrait_template ? right_text
                                   : fm.elidedText(right_text, Qt::ElideMiddle, right_rect.width()));
    }
  };

  draw_time_rnx_row(lo.header_bdt, QString("BDT"), bdt_str);
  draw_time_rnx_row(lo.header_gpst, QString("GPST"), gpst_str);

  QString cand_line;
  if (st.single_candidate_count <= 0) {
    cand_line = gui_i18n_text(in.language, "panel.sats_none");
  } else {
    cand_line = gui_i18n_text(in.language, "panel.sats_prefix");
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
      cand_line +=
          gui_i18n_text(in.language, "panel.sats_more").arg(st.single_candidate_count);
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
    f.setPointSize(clamp_int(std::max(10, (int)std::lround((double)r.h * 0.44)), 10, 14));
    p.setFont(f);
    p.drawText(QRect(r.x, r.y, r.w, r.h), Qt::AlignCenter, text);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setFont(base_font);
  };

  p.setPen(QPen(QColor("#4b5b70"), 2));
  p.drawLine(lo.btn_tab_simple.x, lo.btn_tab_simple.y + lo.btn_tab_simple.h,
             lo.btn_tab_detail.x + lo.btn_tab_detail.w, lo.btn_tab_simple.y + lo.btn_tab_simple.h);
  draw_tab_btn(lo.btn_tab_simple, gui_i18n_text(in.language, "tab.simple"),
               !st.show_detailed_ctrl);
  draw_tab_btn(lo.btn_tab_detail, gui_i18n_text(in.language, "tab.detail"),
               st.show_detailed_ctrl);

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
    std::snprintf(v_prn, sizeof(v_prn), "%s",
                  gui_i18n_text(in.language, "value.prn")
                      .arg(st.single_prn)
                      .toUtf8()
                      .constData());
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
                            QColor(239, 68, 68, 220),
                            gui_i18n_text(in.language, "label.interfere").toUtf8().constData(),
                            gui_i18n_text(in.language, "label.spoof").toUtf8().constData(),
                            gui_i18n_text(in.language, "label.jam").toUtf8().constData(),
                            st.interference_selection, jam_ctrl_enabled);
  }

  control_draw_three_switch(p, lo.sw_sys, color_border, color_text, color_dim,
                            QColor(color_title.red(), color_title.green(), color_title.blue(), 220),
                            gui_i18n_text(in.language, "label.system").toUtf8().constData(),
                            "BDS",
                            "BDS+GPS", "GPS", sys_idx, sys_ctrl_enabled);

  if (!st.show_detailed_ctrl) {
    control_draw_slider_stacked(p, lo.fs_slider, color_border, color_text, color_dim,
                                QColor(59, 130, 246, 220),
                                gui_i18n_text(in.language, "label.fs").toUtf8().constData(), v_fs,
                                fs_ratio, simple_fs_enabled, true, true);
  } else {
    control_draw_slider(p, lo.fs_slider, color_border, color_text, color_dim,
                        QColor(59, 130, 246, 220),
                        gui_i18n_text(in.language, "label.fs").toUtf8().constData(), v_fs,
                        fs_ratio, simple_fs_enabled);
  }

  if (st.show_detailed_ctrl) {
    control_draw_slider(p, lo.gain_slider, color_border, color_text, color_dim,
                        QColor(96, 165, 250, 220),
                        gui_i18n_text(in.language, "label.signal_gain").toUtf8().constData(), v_gain,
                        (st.gain - 0.1) / (20.0 - 0.1), detail_gain_enabled);
  }
  if (!st.show_detailed_ctrl) {
    control_draw_slider_stacked(p, lo.tx_slider, color_border, color_text, color_dim,
                                QColor(56, 189, 248, 220),
                                gui_i18n_text(in.language, "label.tx_gain").toUtf8().constData(), v_tx,
                                st.tx_gain / 100.0, simple_tx_enabled, false, false);
  }

  if (st.show_detailed_ctrl) {
    if (lo.detail_sats.h > 0) {
        QFont detail_info_font = gui_font_mono(
          clamp_int((int)std::lround((double)base_font.pointSize() * 0.92),
              9, 14));
      p.setFont(detail_info_font);
      p.setPen(mode_any ? QColor("#b9cadf") : color_dim);
      p.drawText(QRect(lo.detail_sats.x, lo.detail_sats.y, lo.detail_sats.w, lo.detail_sats.h),
                 Qt::AlignVCenter | Qt::AlignLeft,
                 p.fontMetrics().elidedText(cand_line, Qt::ElideRight, lo.detail_sats.w));
      p.setFont(base_font);
    }

    control_draw_slider(p, lo.cn0_slider, color_border, color_text, color_dim,
                        QColor(14, 165, 233, 220),
                        gui_i18n_text(in.language, "label.target_cn0").toUtf8().constData(), v_cn0,
                        (st.target_cn0 - 20.0) / 40.0, detail_cn0_enabled);
    control_draw_slider(p, lo.seed_slider, color_border, color_text, color_dim,
                        QColor(52, 211, 153, 220),
                        gui_i18n_text(in.language, "label.seed").toUtf8().constData(), v_seed,
                        seed_ratio, detail_other_enabled);
    control_draw_slider(p, lo.prn_slider, color_border, color_text, color_dim,
                        QColor(45, 212, 191, 220),
                        gui_i18n_text(in.language, "label.prn_select").toUtf8().constData(), v_prn,
                        prn_ratio, detail_other_enabled && st.sat_mode == 0);
    control_draw_slider(p, lo.path_v_slider, color_border, color_text, color_dim,
                        QColor(248, 113, 113, 220),
                        gui_i18n_text(in.language, "label.path_vmax").toUtf8().constData(), v_path_v,
                        path_v_ratio, detail_other_enabled);
    control_draw_slider(p, lo.path_a_slider, color_border, color_text, color_dim,
                        QColor(251, 146, 60, 220),
                        gui_i18n_text(in.language, "label.path_acc").toUtf8().constData(), v_path_a,
                        path_a_ratio, detail_other_enabled);
    control_draw_slider(p, lo.ch_slider, color_border, color_text, color_dim,
                        QColor(250, 204, 21, 220),
                        gui_i18n_text(in.language, "label.max_ch").toUtf8().constData(), v_ch,
                        ((double)st.max_ch - 1.0) / 15.0, detail_other_enabled);

    if (st.signal_mode == SIG_MODE_BDS) {
      control_draw_three_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                                QColor(38, 115, 219, 220),
                                gui_i18n_text(in.language, "label.mode").toUtf8().constData(), "SINGLE",
                                "1-37", "1-63", st.sat_mode, detail_mode_enabled);
    } else if (st.signal_mode == SIG_MODE_MIXED) {
      int active_idx = (st.sat_mode == 1) ? 0 : 1;
      control_draw_two_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                              QColor(38, 115, 219, 220),
                              gui_i18n_text(in.language, "label.mode").toUtf8().constData(), "BDS-37+GPS",
                              "BDS-63+GPS", active_idx, detail_mode_enabled);
    } else {
      int active_idx = (st.sat_mode == 0) ? 0 : 1;
      control_draw_two_switch(p, lo.sw_mode, color_border, color_text, color_dim,
                              QColor(38, 115, 219, 220),
                              gui_i18n_text(in.language, "label.mode").toUtf8().constData(), "SINGLE",
                              "1-32", active_idx, detail_mode_enabled);
    }

    control_draw_checkbox(p, lo.tg_meo, color_border, color_text, color_dim,
                          "MEO", st.meo_only, detail_other_enabled);
    control_draw_checkbox(p, lo.tg_iono, color_border, color_text, color_dim,
                          "IONO", st.iono_on, detail_other_enabled);
    control_draw_checkbox(p, lo.tg_clk, color_border, color_text, color_dim,
                          "EXT CLK", st.usrp_external_clk, detail_other_enabled);

    control_draw_two_switch(p, lo.sw_fmt, color_border, color_text, color_dim,
                            QColor(56, 189, 248, 220),
                            gui_i18n_text(in.language, "label.format").toUtf8().constData(), "SHORT",
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
                               color_btn_text_black,
                               gui_i18n_text(in.language, "label.start").toUtf8().constData());
  }

  if (lo.btn_exit.w > 0) {
    bool exit_enabled = !st.running_ui;
    control_draw_button_filled(p, lo.btn_exit,
                               exit_enabled ? color_stop_btn : color_exit_off,
                               exit_enabled ? color_stop_btn : color_exit_off,
                               color_btn_text_black,
                               gui_i18n_text(in.language, "label.exit").toUtf8().constData());
  }
}