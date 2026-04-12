#include "gui/tutorial/overlay/tutorial_overlay_render_utils.h"

#include "gui/control/panel/control_paint.h"
#include "gui/core/i18n/gui_i18n.h"
#include "gui/tutorial/overlay/tutorial_overlay_math_utils.h"

#include <QPainterPath>

#include <algorithm>
#include <cmath>

bool tutorial_overlay_overlaps_existing_box(
    const QRectF &candidate, const std::vector<QRectF> &placed_boxes,
    int step) {
  for (const QRectF &placed : placed_boxes) {
    if (step == 4 || step == 5 || step == 6) {
      const QRectF c = candidate.adjusted(-10.0, -8.0, 10.0, 8.0);
      const QRectF p = placed.adjusted(-10.0, -8.0, 10.0, 8.0);
      if (c.intersects(p)) return true;
    } else {
      if (candidate.intersects(placed)) return true;
    }
  }
  return false;
}

void tutorial_overlay_resolve_overlap_with_placed(
    QRectF *box, const std::vector<QRectF> &placed_boxes, const QRectF &ray_rect,
    int step, int win_width, int win_height) {
  if (!box) return;
  if (!tutorial_overlay_overlaps_existing_box(*box, placed_boxes, step)) return;

  QPointF sep_dir = box->center() - ray_rect.center();
  const double sep_len = std::hypot(sep_dir.x(), sep_dir.y());
  if (sep_len > 1e-6) {
    sep_dir = QPointF(sep_dir.x() / sep_len, sep_dir.y() / sep_len);
  } else {
    sep_dir = QPointF(1.0, 0.0);
  }

  int iter = 0;
  while (tutorial_overlay_overlaps_existing_box(*box, placed_boxes, step) &&
         iter < 80) {
    const QPointF tangential(-sep_dir.y(), sep_dir.x());
    const double tan_step = ((iter % 2) == 0 ? 1.0 : -1.0) *
                            (step == 2 ? 3.0
                                       : ((step == 5 || step == 6 ||
                                           step == 7 || step == 8)
                                              ? 4.0
                                              : 2.0));
    box->translate(sep_dir.x() * 9.0 + tangential.x() * tan_step,
                   sep_dir.y() * 9.0 + tangential.y() * tan_step);

    if (box->left() < 10.0) box->moveLeft(10.0);
    if (box->top() < 10.0) box->moveTop(10.0);
    if (box->right() > win_width - 10.0) box->moveRight(win_width - 10.0);
    if (box->bottom() > win_height - 58.0) box->moveBottom(win_height - 58.0);
    ++iter;
  }
}

QPointF tutorial_overlay_draw_callout_connector(
    QPainter &p, const QRectF &ray_rect, const QRectF &box,
    const QPointF &anchor_override, bool use_anchor_override, int step,
    double pulse) {
  QPointF anchor = rect_edge_anchor_toward(ray_rect, box.center());
  if (step >= 1 && step <= 8 && use_anchor_override) {
    anchor = anchor_override;
  }

  const QPointF target = box.center();
  if (step == 1) {
    const QPointF v = target - anchor;
    const double len = std::max(1e-6, std::hypot(v.x(), v.y()));
    const QPointF n(-v.y() / len, v.x() / len);
    const double bend = std::min(24.0, len * 0.08);
    const QPointF ctrl = (anchor + target) * 0.5 + n * bend;

    QPainterPath path(anchor);
    path.quadTo(ctrl, target);

    const int glow_alpha = 55 + (int)std::lround(32.0 * pulse);
    const int core_alpha = 188 + (int)std::lround(52.0 * pulse);
    p.setPen(QPen(QColor(56, 189, 248, glow_alpha), 4.0, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);

    p.setPen(QPen(QColor(125, 211, 252, core_alpha), 2.0, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
  } else {
    QPainterPath path(anchor);
    path.lineTo(target);

    const int glow_alpha = 55 + (int)std::lround(28.0 * pulse);
    const int core_alpha = 188 + (int)std::lround(48.0 * pulse);
    p.setPen(QPen(QColor(56, 189, 248, glow_alpha), 3.8, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.setPen(QPen(QColor(125, 211, 252, core_alpha), 2.0, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
  }

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(56, 189, 248, 170));
  p.drawEllipse(anchor, 5.2, 5.2);
  p.setBrush(QColor(229, 238, 252, 235));
  p.drawEllipse(anchor, 2.5, 2.5);
  p.setBrush(QColor(56, 189, 248, 190));
  p.drawEllipse(target, 4.6, 4.6);
  p.setBrush(QColor(229, 238, 252, 235));
  p.drawEllipse(target, 2.1, 2.1);

  return anchor;
}

void tutorial_overlay_adjust_callout_box_height_for_text(
    QRectF *box, const TutorialGalaxyCalloutDef &def, const QFont &text_font,
    int win_height) {
  if (!box) return;
  const int nl_idx = def.text.indexOf('\n');
  if (nl_idx < 0) return;

  const QString pre_body = def.text.mid(nl_idx + 1);
  QFont bf = text_font;
  bf.setPointSize(std::max(8, text_font.pointSize() - 1));
  QFontMetrics bfm(bf);
  const int avail_w = std::max(60, (int)box->width() - 20);
  const QRect needed = bfm.boundingRect(
      QRect(0, 0, avail_w, 4000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
      pre_body);

  const double min_h = 34.0 + needed.height() + 5.0;
  if (box->height() < min_h) {
    box->setHeight(min_h);
    if (box->bottom() > win_height - 58.0) box->moveBottom(win_height - 58.0);
    if (box->top() < 10.0) box->moveTop(10.0);
  }
}

void tutorial_overlay_draw_callout_box_and_text(
    QPainter &p, const QRectF &box, const TutorialGalaxyCalloutDef &def,
    const QFont &text_font) {
  p.setPen(QPen(QColor(186, 230, 253, 210), 1));
  p.setBrush(QColor(8, 20, 38, 236));
  p.drawRoundedRect(box, 9, 9);

  const int nl_idx = def.text.indexOf('\n');
  const QString c_title = (nl_idx >= 0) ? def.text.left(nl_idx) : def.text;
  const QString c_body = (nl_idx >= 0) ? def.text.mid(nl_idx + 1) : QString();
  const QRectF inner = box.adjusted(10, 5, -10, -5);

  if (c_body.isEmpty()) {
    p.setPen(QColor("#e5eefc"));
    p.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
               c_title);
    return;
  }

  QFont tf = text_font;
  p.setFont(tf);
  p.setPen(QColor(125, 211, 252));
  p.drawText(QRectF(inner.x(), inner.y(), inner.width(), 22),
             Qt::AlignLeft | Qt::AlignVCenter, c_title);

  QFont bf = text_font;
  bf.setPointSize(std::max(8, text_font.pointSize() - 1));
  p.setFont(bf);
  p.setPen(QColor(200, 220, 245));
  p.drawText(QRectF(inner.x(), inner.y() + 24, inner.width(), inner.height() - 24),
             Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, c_body);
  p.setFont(text_font);
}

void tutorial_overlay_draw_nav_buttons(QPainter &p, const TutorialOverlayInput &in,
                                       const TutorialOverlayState &state,
                                       int step) {
  auto draw_overlay_btn = [&](const QRect &r, const QColor &fill,
                              const QColor &text, const char *label) {
    Rect rr{r.x(), r.y(), r.width(), r.height()};
    control_draw_button_filled(p, rr, fill, fill, text, label);
  };

  const QByteArray contents_label =
      gui_i18n_text(in.language, "tutorial.btn.contents").toUtf8();
  const QByteArray prev_label =
      gui_i18n_text(in.language, "tutorial.btn.prev").toUtf8();
  const QByteArray next_label =
      gui_i18n_text(in.language, "tutorial.btn.next").toUtf8();
  const QByteArray done_label =
      gui_i18n_text(in.language, "tutorial.btn.done").toUtf8();
  const QByteArray exit_label =
      gui_i18n_text(in.language, "tutorial.btn.exit").toUtf8();

  {
    const QFont saved_btn_font = p.font();
    QFont contents_font = saved_btn_font;
    contents_font.setPointSize(std::max(8, saved_btn_font.pointSize() - 2));
    p.setFont(contents_font);
    draw_overlay_btn(state.contents_btn_rect, QColor("#1e3a5f"),
                     QColor("#7dd3fc"), contents_label.constData());
    p.setFont(saved_btn_font);
  }
  {
    draw_overlay_btn(state.prev_btn_rect, QColor("#334155"), QColor("#f8fbff"),
                     prev_label.constData());
  }
  if (step < in.last_step) {
    draw_overlay_btn(state.next_btn_rect, QColor("#0ea5e9"), QColor(8, 12, 18),
                     next_label.constData());
  } else {
    draw_overlay_btn(state.next_btn_rect, QColor("#22c55e"), QColor(8, 12, 18),
                     done_label.constData());
  }
  {
    draw_overlay_btn(state.close_btn_rect, QColor("#6b7280"), QColor("#f8fbff"),
                     exit_label.constData());
  }
}
