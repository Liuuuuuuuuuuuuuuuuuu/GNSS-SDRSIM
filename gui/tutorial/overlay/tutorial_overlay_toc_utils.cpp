#include "gui/tutorial/overlay/tutorial_overlay_toc_utils.h"

#include "gui/control/panel/control_paint.h"
#include "gui/core/i18n/gui_font_manager.h"
#include "gui/core/i18n/gui_i18n.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

namespace {

struct TocEntry {
  const char *label_key;
  const char *range_key;
  int r;
  int g;
  int b;
};

} // namespace

bool tutorial_draw_toc_overlay(QPainter &p, const TutorialOverlayInput &in,
                               TutorialOverlayState *state,
                               bool *capture_in_progress,
                               bool *session_frozen, QWidget **frozen_host,
                               QSize *frozen_size,
                               QPixmap *frozen_background) {
  if (!state || !capture_in_progress || !session_frozen || !frozen_host ||
      !frozen_size || !frozen_background) {
    return false;
  }
  if (in.step != 0) {
    return false;
  }

  {
    const bool need_freeze_toc =
        !*session_frozen || *frozen_host != in.host_widget ||
        *frozen_size != QSize(in.win_width, in.win_height) ||
        frozen_background->isNull();
    if (need_freeze_toc && in.host_widget && !*capture_in_progress) {
      *capture_in_progress = true;
      QWidget *top_widget = in.host_widget->window();
      QScreen *screen = nullptr;
      if (top_widget && top_widget->windowHandle()) {
        screen = top_widget->windowHandle()->screen();
      }
      if (!screen) {
        screen = QGuiApplication::primaryScreen();
      }
      if (screen) {
        const QPoint tlg = in.host_widget->mapToGlobal(QPoint(0, 0));
        *frozen_background = screen->grabWindow(0, tlg.x(), tlg.y(),
                                                in.host_widget->width(),
                                                in.host_widget->height());
        *frozen_host = in.host_widget;
        *frozen_size = QSize(in.win_width, in.win_height);
        *session_frozen = !frozen_background->isNull();
      }
      *capture_in_progress = false;
    }
  }

  const int fs = std::max(10, std::min(14, in.win_height / 60));
  QFont toc_font = gui_font_ui(in.language, fs);

  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);

  if (!frozen_background->isNull()) {
    p.setOpacity(0.07);
    p.drawPixmap(0, 0, *frozen_background);
    p.setOpacity(1.0);
  }

  QLinearGradient bg_grad(0, 0, 0, in.win_height);
  bg_grad.setColorAt(0.0, QColor(4, 8, 20, 242));
  bg_grad.setColorAt(0.5, QColor(5, 12, 30, 232));
  bg_grad.setColorAt(1.0, QColor(4, 8, 20, 246));
  p.fillRect(QRect(0, 0, in.win_width, in.win_height), bg_grad);

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(56, 189, 248, 18));
  const int gs = 38;
  for (int gx = gs / 2; gx < in.win_width + gs; gx += gs) {
    for (int gy = gs / 2; gy < in.win_height + gs; gy += gs) {
      p.drawEllipse(QPointF(gx, gy), 1.4, 1.4);
    }
  }

  const int hdr_h = 52;
  const int ftr_h = 52;
  p.setPen(QPen(QColor(56, 189, 248, 36), 1));
  p.drawLine(0, hdr_h - 1, in.win_width, hdr_h - 1);
  p.drawLine(0, in.win_height - ftr_h, in.win_width, in.win_height - ftr_h);

  QFont hdr_font = toc_font;
  hdr_font.setLetterSpacing(QFont::AbsoluteSpacing, 2.8);
  hdr_font.setPointSize(fs + 1);
  p.setFont(hdr_font);
  p.setPen(QColor(125, 211, 252));
  const QString hdr_txt =
      gui_i18n_text(in.language, "tutorial.overlay.toc_header");
  p.drawText(QRect(0, 0, in.win_width, hdr_h - 1),
             Qt::AlignHCenter | Qt::AlignVCenter, hdr_txt);

  static const TocEntry kE[5] = {
      {"tutorial.toc.section.map", "tutorial.toc.range.map", 30, 120, 220},
      {"tutorial.toc.section.skyplot", "tutorial.toc.range.skyplot", 30, 180,
       90},
      {"tutorial.toc.section.waveforms", "tutorial.toc.range.waveforms", 200,
       100, 20},
      {"tutorial.toc.section.simple", "tutorial.toc.range.simple", 130, 50,
       200},
      {"tutorial.toc.section.detail", "tutorial.toc.range.detail", 20, 140,
       180},
  };
  static const int kTargets[5] = {1, 3, 4, 5, 7};

  const int btn_bar_h = 46;
  const int avail_h = in.win_height - hdr_h - ftr_h - btn_bar_h;
  const int bh = std::min(76, std::max(50, avail_h / 6));
  const int bgap = std::max(8, bh / 7);
  const int total_cards_h = 5 * bh + 4 * bgap;
  const int bw = std::min(580, std::max(320, (int)(in.win_width * 0.58)));
  const int bx = (in.win_width - bw) / 2;
  const int base_y = hdr_h + (avail_h - total_cards_h) / 2;

  const int badge_r = std::max(12, bh / 2 - 9);
  const int badge_diam = badge_r * 2;
  const int pill_w = 116;
  const int label_start_x = bx + 4 + badge_diam + 16;
  const int label_end_x = bx + bw - pill_w - 14;

  QFont badge_f = toc_font;
  badge_f.setPointSize(std::max(8, fs - 1));
  QFont label_f = toc_font;
  QFont pill_f = toc_font;
  pill_f.setPointSize(std::max(8, fs - 2));

  for (int i = 0; i < 5; ++i) {
    state->toc_btn_targets[i] = kTargets[i];
    const QRect btn(bx, base_y + i * (bh + bgap), bw, bh);
    state->toc_btn_rects[i] = btn;
    const int cy = btn.y() + btn.height() / 2;
    const int r = kE[i].r;
    const int g = kE[i].g;
    const int b = kE[i].b;

    p.setBrush(QColor(r / 9, g / 9, b / 9 + 5, 225));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(btn, 7, 7);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(r, g, b, 48), 3));
    p.drawRoundedRect(btn.adjusted(-2, -2, 2, 2), 9, 9);

    p.setPen(QPen(QColor(r, g, b, 135), 1.5));
    p.drawRoundedRect(btn, 7, 7);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(r, g, b, 210));
    p.drawRoundedRect(QRect(btn.x(), btn.y() + 5, 4, btn.height() - 10), 2, 2);

    const QPointF badge_c(bx + 4 + badge_r + 8, cy);
    p.setBrush(QColor(r, g, b, 40));
    p.setPen(QPen(QColor(r, g, b, 165), 1.5));
    p.drawEllipse(badge_c, (double)badge_r, (double)badge_r);

    p.setFont(badge_f);
    const int br = std::min(255, r + 85);
    const int bg = std::min(255, g + 85);
    const int bb = std::min(255, b + 85);
    p.setPen(QColor(br, bg, bb));
    p.drawText(QRectF(badge_c.x() - badge_r, badge_c.y() - badge_r, badge_diam,
                      badge_diam),
               Qt::AlignCenter, QString::number(kTargets[i]));

    p.setFont(label_f);
    p.setPen(QColor(228, 244, 255));
    const QString lbl = gui_i18n_text(in.language, kE[i].label_key);
    p.drawText(QRect(label_start_x, btn.y(), label_end_x - label_start_x, bh),
               Qt::AlignLeft | Qt::AlignVCenter, lbl);

    const QRect pill(bx + bw - pill_w - 6, cy - 11, pill_w, 22);
    p.setBrush(QColor(r, g, b, 28));
    p.setPen(QPen(QColor(r, g, b, 100), 1));
    p.drawRoundedRect(pill, 11, 11);
    p.setFont(pill_f);
    const int pr = std::min(255, r + 65);
    const int pg = std::min(255, g + 65);
    const int pb = std::min(255, b + 65);
    p.setPen(QColor(pr, pg, pb, 210));
    const QString rng_str = gui_i18n_text(in.language, kE[i].range_key);
    p.drawText(pill, Qt::AlignCenter, rng_str);
  }

  const int nbw = 98;
  const int nbh = 28;
  const int nbgap = 12;
  const int nbar_y = in.win_height - nbh - 14;
  const int ntotal_w = nbw * 3 + nbgap * 2;
  const int nbar_x = std::max(10, (in.win_width - ntotal_w) / 2);
  state->prev_btn_rect = QRect(nbar_x, nbar_y, nbw, nbh);
  state->next_btn_rect = QRect(nbar_x + nbw + nbgap, nbar_y, nbw, nbh);
  state->close_btn_rect =
      QRect(nbar_x + (nbw + nbgap) * 2, nbar_y, nbw, nbh);
  state->contents_btn_rect = QRect();

  auto draw_toc_nav = [&](const QRect &r, const QColor &fill, const QColor &tc,
                          const char *s) {
    Rect rr{r.x(), r.y(), r.width(), r.height()};
    control_draw_button_filled(p, rr, fill, fill, tc, s);
  };
    const QByteArray prev_label =
      gui_i18n_text(in.language, "tutorial.btn.prev").toUtf8();
    const QByteArray next_label =
      gui_i18n_text(in.language, "tutorial.btn.next").toUtf8();
    const QByteArray exit_label =
      gui_i18n_text(in.language, "tutorial.btn.exit").toUtf8();
  p.setFont(toc_font);
  draw_toc_nav(
      state->prev_btn_rect, QColor("#334155"), QColor("#f8fbff"),
      prev_label.constData());
  draw_toc_nav(
      state->next_btn_rect, QColor("#0ea5e9"), QColor(8, 12, 18),
      next_label.constData());
  draw_toc_nav(
      state->close_btn_rect, QColor("#6b7280"), QColor("#f8fbff"),
      exit_label.constData());

  state->text_page_count = 1;
  if (state->text_page >= 1) {
    state->text_page = 0;
  }
  state->callout_hit_boxes.clear();
  state->callout_hit_anchors.clear();

  p.restore();
  return true;
}