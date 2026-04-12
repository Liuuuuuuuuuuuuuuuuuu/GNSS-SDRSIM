#include "gui/tutorial/flow/tutorial_flow_utils.h"

#include "gui/core/i18n/gui_i18n.h"

namespace {

const int kStepCount = 9;

int clamp_step(int step) {
  if (step < 0)
    return 0;
  if (step >= kStepCount)
    return kStepCount - 1;
  return step;
}

} // namespace

int tutorial_last_step() {
  return kStepCount - 1;
}

int tutorial_spotlight_count_for_step(int step) {
  (void)step;
  return 0;
}

QString tutorial_step_title(int step, GuiLanguage language) {
  switch (clamp_step(step)) {
  case 0:
    return gui_i18n_text(language, "tutorial.flow.title.0");
  case 1:
    return gui_i18n_text(language, "tutorial.flow.title.1");
  case 2:
    return gui_i18n_text(language, "tutorial.flow.title.2");
  case 3:
    return gui_i18n_text(language, "tutorial.flow.title.3");
  case 4:
    return gui_i18n_text(language, "tutorial.flow.title.4");
  case 5:
    return gui_i18n_text(language, "tutorial.flow.title.5");
  case 6:
    return gui_i18n_text(language, "tutorial.flow.title.6");
  case 7:
    return gui_i18n_text(language, "tutorial.flow.title.7");
  case 8:
  default:
    return gui_i18n_text(language, "tutorial.flow.title.8");
  }
}

QString tutorial_step_body(int step, bool detailed, bool running_ui,
                           GuiLanguage language) {
  (void)detailed;
  (void)running_ui;
  switch (clamp_step(step)) {
  case 0:
    return gui_i18n_text(language, "tutorial.flow.body.0");
  case 1:
    return gui_i18n_text(language, "tutorial.flow.body.1");
  case 2:
    return gui_i18n_text(language, "tutorial.flow.body.2");
  case 3:
    return gui_i18n_text(language, "tutorial.flow.body.3");
  case 4:
    return gui_i18n_text(language, "tutorial.flow.body.4");
  case 5:
    return gui_i18n_text(language, "tutorial.flow.body.5");
  case 6:
    return gui_i18n_text(language, "tutorial.flow.body.6");
  case 7:
    return gui_i18n_text(language, "tutorial.flow.body.7");
  case 8:
  default:
    return gui_i18n_text(language, "tutorial.flow.body.8");
  }
}
