#include "gui/tutorial/tutorial_overlay_utils.h"

#include "gui/layout/control_layout.h"
#include "gui/control/control_paint.h"
#include "gui/tutorial/tutorial_flow_utils.h"

#include <QFontMetrics>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct TutorialSpotlightItem {
  QRect rect;
  QString name;
  QString desc;
};

QRect rect_from(const Rect &r) {
  return QRect(r.x, r.y, r.w, r.h);
}

std::vector<QString> paginate_text_to_rect(const QString &src, const QRect &r,
                                           const QFont &f) {
  QFontMetrics fm(f);
  QString normalized = src;
  normalized.replace("\r\n", "\n");
  QStringList words = normalized.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

  std::vector<QString> pages;
  if (words.isEmpty()) {
    pages.push_back(QString());
    return pages;
  }

  QString current;
  int i = 0;
  while (i < words.size()) {
    QString candidate = current.isEmpty() ? words[i] : (current + " " + words[i]);
    QRect br = fm.boundingRect(r, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, candidate);
    if (br.height() <= r.height()) {
      current = candidate;
      ++i;
      continue;
    }

    if (current.isEmpty()) {
      current = words[i];
      ++i;
    }
    pages.push_back(current);
    current.clear();
  }

  if (!current.isEmpty()) {
    pages.push_back(current);
  }

  if (pages.empty()) pages.push_back(src);
  return pages;
}

} // namespace

QRect tutorial_focus_rect_for_step(const TutorialOverlayInput &in) {
  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.detailed);

  if (in.step == 1) {
    return in.osm_panel_rect.adjusted(10, 46, -10, -16);
  }
  if (in.step == 4) {
    int map_x = in.win_width / 2;
    int map_y = 0;
    int map_w = in.win_width - map_x;
    int map_h = in.win_height / 2;
    return QRect(map_x + 6, map_y + 6, std::max(10, map_w - 12), std::max(10, map_h - 12));
  }
  if (in.step == 2) return QRect(lo.btn_tab_simple.x, lo.btn_tab_simple.y, lo.btn_tab_simple.w, lo.btn_tab_simple.h);
  if (in.step == 3) return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  if (in.step == 5) return QRect(lo.btn_tab_detail.x, lo.btn_tab_detail.y, lo.btn_tab_detail.w, lo.btn_tab_detail.h);
  if (in.step == 6) return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  if (in.step == 7) {
    QRect ch_rect(lo.ch_slider.x, lo.ch_slider.y, lo.ch_slider.w, lo.ch_slider.h);
    if (ch_rect.width() > 4 && ch_rect.height() > 4) return ch_rect;
    QRect detail_tab(lo.btn_tab_detail.x, lo.btn_tab_detail.y, lo.btn_tab_detail.w, lo.btn_tab_detail.h);
    if (detail_tab.width() > 4 && detail_tab.height() > 4) return detail_tab;
    return QRect(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h);
  }
  if (in.step == 8) {
    if (in.running_ui) {
      if (!in.osm_stop_btn_rect.isEmpty()) return in.osm_stop_btn_rect;
      return QRect(in.osm_panel_rect.x() + (in.osm_panel_rect.width() - 220) / 2,
                   in.osm_panel_rect.y() + in.osm_panel_rect.height() - 110,
                   220, 48);
    }
    return QRect(lo.btn_start.x, lo.btn_start.y, lo.btn_start.w, lo.btn_start.h);
  }
  if (in.step == 13) {
    if (!in.osm_stop_btn_rect.isEmpty()) return in.osm_stop_btn_rect;
    return QRect(in.osm_panel_rect.x() + (in.osm_panel_rect.width() - 220) / 2,
                 in.osm_panel_rect.y() + in.osm_panel_rect.height() - 110,
                 220, 48);
  }
  if (in.step == 9 || in.step == 10 || in.step == 11 || in.step == 12) {
    int x = in.win_width / 2;
    int y = in.win_height / 2;
    int w = in.win_width - x;
    int h = in.win_height - y;
    if (in.step == 9) {
      return QRect(x + 6, y + 6, std::max(10, w / 2 - 12), std::max(10, h / 2 - 12));
    }
    if (in.step == 10) {
      return QRect(x + w / 2 + 6, y + 6, std::max(10, w / 2 - 12), std::max(10, h / 2 - 12));
    }
    if (in.step == 11) {
      return QRect(x + 6, y + h / 2 + 6, std::max(10, w / 2 - 12), std::max(10, h / 2 - 12));
    }
    return QRect(x + w / 2 + 6, y + h / 2 + 6, std::max(10, w / 2 - 12), std::max(10, h / 2 - 12));
  }
  if (in.step == 14) {
    return QRect(lo.btn_exit.x, lo.btn_exit.y, lo.btn_exit.w, lo.btn_exit.h);
  }
  return QRect();
}

void tutorial_draw_overlay(QPainter &p, const TutorialOverlayInput &in,
                           TutorialOverlayState *state) {
  if (!state) return;
  if (!in.overlay_visible) {
    state->prev_btn_rect = QRect();
    state->next_btn_rect = QRect();
    state->close_btn_rect = QRect();
    return;
  }

  const int step = std::max(0, std::min(in.step, in.last_step));
  TutorialOverlayInput focus_in = in;
  focus_in.step = step;
  QRect focus = tutorial_focus_rect_for_step(focus_in);

  auto now_tp = std::chrono::steady_clock::now();
  if (state->anim_step_anchor != step) {
    state->anim_step_anchor = step;
    state->anim_start_tp = now_tp;
    state->spotlight_index = 0;
  }
  const double anim_sec = std::chrono::duration<double>(now_tp - state->anim_start_tp).count();
  const double pulse = 0.5 + 0.5 * std::sin(anim_sec * 2.0 * 3.141592653589793 * 0.95);
  const int halo_expand = 4 + (int)std::lround(5.0 * pulse);
  const int halo_alpha = 76 + (int)std::lround(72.0 * pulse);
  const int ring_alpha = 170 + (int)std::lround(70.0 * pulse);

  ControlLayout lo;
  compute_control_layout(in.win_width, in.win_height, &lo, in.detailed);

  std::vector<TutorialSpotlightItem> items;
  if (step == 3) {
    items.push_back({rect_from(lo.sw_sys), "SYS", "Choose signal family: BDS, BDS+GPS, or GPS."});
    items.push_back({rect_from(lo.fs_slider), "Fs", "Set sample rate. It must match the minimum required by SYS."});
    items.push_back({rect_from(lo.tx_slider), "Tx Gain", "Set RF output strength. Start lower, then increase if needed."});
  } else if (step == 6) {
    items.push_back({rect_from(lo.gain_slider), "Gain", "Fine receiver-side strength tuning for simulation profile."});
    items.push_back({rect_from(lo.cn0_slider), "CN0", "Target carrier-to-noise level. Higher means cleaner signal."});
    items.push_back({rect_from(lo.prn_slider), "PRN", "Choose satellite PRN for focused test scenarios."});
    items.push_back({rect_from(lo.seed_slider), "Seed", "Random seed for reproducible test behavior."});
    items.push_back({rect_from(lo.ch_slider), "CH", "Limit active channels. Useful for load and edge-case tests."});
  } else if (step == 7) {
    items.push_back({rect_from(lo.sw_mode), "MODE", "Choose satellite selection behavior for this run."});
    items.push_back({rect_from(lo.sw_fmt), "FORMAT", "Choose output sample format: SHORT or BYTE."});
    items.push_back({rect_from(lo.tg_meo), "MEO", "Toggle MEO-only behavior when your test needs it."});
    items.push_back({rect_from(lo.tg_iono), "IONO", "Toggle ionospheric effect model on or off."});
    items.push_back({rect_from(lo.tg_clk), "EXT CLK", "Toggle external clock source usage for hardware sync."});
  }

  QRect spotlight_rect;
  QString spotlight_name;
  QString spotlight_desc;
  if (!items.empty()) {
    const int spotlight_total = (int)items.size();
    int idx = state->spotlight_index;
    if (idx < 0) idx = 0;
    if (idx >= spotlight_total) idx = spotlight_total - 1;
    state->spotlight_index = idx;
    spotlight_rect = items[idx].rect;
    spotlight_name = items[idx].name;
    spotlight_desc = items[idx].desc;
  }

  if (!focus.isEmpty()) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(125, 211, 252, halo_alpha), 4));
    p.drawRoundedRect(focus.adjusted(-halo_expand, -halo_expand, halo_expand, halo_expand),
                      10 + halo_expand / 2, 10 + halo_expand / 2);
    p.setPen(QPen(QColor(186, 230, 253, ring_alpha), 2));
    p.drawRoundedRect(focus.adjusted(-3, -3, 3, 3), 8, 8);
    p.setRenderHint(QPainter::Antialiasing, false);
  }

  if (!spotlight_rect.isEmpty()) {
    int ex = 4 + (int)std::lround(2.0 * pulse);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(56, 189, 248, 160 + (int)std::lround(70.0 * pulse)), 3));
    p.drawRoundedRect(spotlight_rect.adjusted(-ex, -ex, ex, ex), 9, 9);
    p.setPen(QPen(QColor(186, 230, 253, 130 + (int)std::lround(60.0 * pulse)), 1));
    p.drawRoundedRect(spotlight_rect.adjusted(-ex - 3, -ex - 3, ex + 3, ex + 3), 10, 10);
    p.setRenderHint(QPainter::Antialiasing, false);
  }

  int card_w = std::max(560, std::min(920, (int)std::lround((double)in.win_width * 0.50)));
  int card_h = std::max(340, std::min(560, (int)std::lround((double)in.win_height * 0.48)));
  QRect card(in.win_width - card_w - 24, in.win_height - card_h - 24, card_w, card_h);
  {
    std::vector<QRect> candidates;
    candidates.push_back(QRect(in.win_width - card_w - 24, in.win_height - card_h - 24, card_w, card_h));
    candidates.push_back(QRect(in.win_width - card_w - 24, 20, card_w, card_h));
    candidates.push_back(QRect(24, 20, card_w, card_h));
    candidates.push_back(QRect(24, in.win_height - card_h - 24, card_w, card_h));

    QRect avoid = focus.isEmpty() ? QRect() : focus.adjusted(-18, -18, 18, 18);
    QRect chosen = card;
    int best_score = -1;
    for (const QRect &cand : candidates) {
      QRect c = cand;
      if (c.right() > in.win_width - 8) c.moveRight(in.win_width - 8);
      if (c.bottom() > in.win_height - 8) c.moveBottom(in.win_height - 8);
      if (c.left() < 8) c.moveLeft(8);
      if (c.top() < 8) c.moveTop(8);

      int score = 0;
      if (!avoid.isEmpty()) {
        if (!c.intersects(avoid)) score += 1000;
        int dx = std::abs(c.center().x() - avoid.center().x());
        int dy = std::abs(c.center().y() - avoid.center().y());
        score += (dx + dy);
      }
      if (!in.osm_stop_btn_rect.isEmpty()) {
        if (c.intersects(in.osm_stop_btn_rect)) score -= 2500;
        else score += 600;
      }
      if (step == 5 && c.intersects(QRect(focus.x() - 30, focus.y() - 30, focus.width() + 60, focus.height() + 60))) {
        score -= 2000;
      }
      if (score > best_score) {
        best_score = score;
        chosen = c;
      }
    }
    card = chosen;
  }

  p.setRenderHint(QPainter::Antialiasing, true);
  p.setPen(QPen(QColor("#dbeafe"), 1));
  p.setBrush(QColor(8, 18, 34, 238));
  p.drawRoundedRect(card, 10, 10);
  p.setRenderHint(QPainter::Antialiasing, false);

  QFont old_font = p.font();
  int guide_base_pt = std::max(11, std::min(16, in.win_height / 62));

  QFont title_font = old_font;
  title_font.setFamily("Noto Sans");
  title_font.setBold(true);
  title_font.setPointSize(guide_base_pt + 2);
  p.setFont(title_font);
  p.setPen(QColor("#7dd3fc"));
  p.drawText(card.adjusted(14, 12, -14, -card.height() + 40), Qt::AlignLeft | Qt::AlignVCenter,
             QString("Tutorial %1 / %2  %3").arg(step + 1).arg(in.last_step + 1).arg(tutorial_step_title(step)));

  QFont body_font = old_font;
  body_font.setFamily("Noto Sans");
  body_font.setPointSize(guide_base_pt);
  p.setFont(body_font);
  p.setPen(QColor("#e5eefc"));
  QRect body_rect = card.adjusted(14, 48, -14, -74);
  QString body_text = tutorial_step_body(step, in.detailed, in.running_ui);
  if (!spotlight_name.isEmpty()) {
    body_text += QString("\n\nFocus %1/%2: %3\n%4\nHighlighted frame marks the target control.")
                     .arg(state->spotlight_index + 1)
                     .arg(items.size())
                     .arg(spotlight_name)
                     .arg(spotlight_desc);
  }

  if (state->text_page_anchor_step != step || state->text_page_anchor_spotlight != state->spotlight_index) {
    state->text_page_anchor_step = step;
    state->text_page_anchor_spotlight = state->spotlight_index;
    state->text_page = 0;
  }

  std::vector<QString> pages = paginate_text_to_rect(body_text, body_rect, body_font);
  state->text_page_count = (int)pages.size();
  if (state->text_page_count < 1) state->text_page_count = 1;
  if (state->text_page < 0) state->text_page = 0;
  if (state->text_page >= state->text_page_count) state->text_page = state->text_page_count - 1;

  const QString page_text = pages.empty() ? body_text : pages[state->text_page];
  p.drawText(body_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, page_text);

  if (state->text_page_count > 1) {
    p.setPen(QColor("#93c5fd"));
    p.drawText(QRect(card.x() + 14, card.bottom() - 56, 180, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QString("Page %1 / %2").arg(state->text_page + 1).arg(state->text_page_count));
  }

  auto draw_overlay_btn = [&](const QRect &r, const QColor &fill, const QColor &text, const char *label) {
    Rect rr{r.x(), r.y(), r.width(), r.height()};
    control_draw_button_filled(p, rr, fill, fill, text, label);
  };

  state->prev_btn_rect = QRect(card.x() + 12, card.bottom() - 34, 88, 24);
  state->next_btn_rect = QRect(card.x() + 108, card.bottom() - 34, 88, 24);
  state->close_btn_rect = QRect(card.right() - 100, card.bottom() - 34, 88, 24);

  QFont button_font = old_font;
  button_font.setFamily("Noto Sans");
  button_font.setBold(true);
  button_font.setPointSize(std::max(10, guide_base_pt - 1));
  p.setFont(button_font);

  draw_overlay_btn(state->prev_btn_rect, QColor("#334155"), QColor("#f8fbff"), "PREV");
  if (step < in.last_step) {
    draw_overlay_btn(state->next_btn_rect, QColor("#0ea5e9"), QColor(8, 12, 18), "NEXT");
  } else {
    draw_overlay_btn(state->next_btn_rect, QColor("#22c55e"), QColor(8, 12, 18), "DONE");
  }
  draw_overlay_btn(state->close_btn_rect, QColor("#ef4444"), QColor(8, 12, 18), "CLOSE");
  p.setFont(old_font);
}