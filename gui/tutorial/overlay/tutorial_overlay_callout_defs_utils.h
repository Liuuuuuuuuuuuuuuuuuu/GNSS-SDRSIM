#ifndef TUTORIAL_OVERLAY_CALLOUT_DEFS_UTILS_H
#define TUTORIAL_OVERLAY_CALLOUT_DEFS_UTILS_H

#include <vector>

#include "gui/tutorial/overlay/tutorial_overlay_utils.h"

std::vector<TutorialGalaxyCalloutDef>
tutorial_overlay_build_step_callouts(const TutorialOverlayInput &in, int step);

void tutorial_overlay_scale_guide_callouts(
    std::vector<TutorialGalaxyCalloutDef> *callouts);

#endif
