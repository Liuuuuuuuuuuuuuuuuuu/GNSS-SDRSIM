#ifndef TUTORIAL_OVERLAY_TOC_UTILS_H
#define TUTORIAL_OVERLAY_TOC_UTILS_H

#include <QPainter>
#include <QPixmap>
#include <QSize>

#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

class QWidget;

// Renders step-0 contents page and returns true when the frame is fully handled.
bool tutorial_draw_toc_overlay(QPainter &p, const TutorialOverlayInput &in,
                               TutorialOverlayState *state,
                               bool *capture_in_progress,
                               bool *session_frozen, QWidget **frozen_host,
                               QSize *frozen_size,
                               QPixmap *frozen_background);

#endif