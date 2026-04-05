#include "gui/tutorial/tutorial_flow_utils.h"

#include "gui/core/gui_i18n.h"

namespace {

const int kStepCount = 15;

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
  if (step == 3)
    return 3;
  if (step == 6 || step == 7)
    return 5;
  return 0;
}

QString tutorial_step_title(int step, GuiLanguage language) {
  return gui_i18n_text(language,
                       QString("tutorial.step.title.%1").arg(clamp_step(step)).toUtf8().constData());
}

QString tutorial_step_body(int step, bool detailed, bool running_ui,
                          GuiLanguage language) {
  switch (clamp_step(step)) {
  case 0:
    return gui_i18n_text(language, "tutorial.step.body.0");
  case 1:
    return gui_i18n_text(language, "tutorial.step.body.1");
  case 2:
    return gui_i18n_text(language, "tutorial.step.body.2");
  case 3:
    return gui_i18n_text(language, "tutorial.step.body.3");
  case 4:
    return gui_i18n_text(language, "tutorial.step.body.4");
  case 5:
    return gui_i18n_text(language, "tutorial.step.body.5");
  case 6:
    return gui_i18n_text(language, "tutorial.step.body.6");
  case 7:
    return detailed
               ? gui_i18n_text(language, "tutorial.step.body.7.detail")
               : gui_i18n_text(language, "tutorial.step.body.7.simple");
  case 8:
    return running_ui
               ? gui_i18n_text(language, "tutorial.step.body.8.running")
               : gui_i18n_text(language, "tutorial.step.body.8.idle");
  case 9:
    return gui_i18n_text(language, "tutorial.step.body.9");
  case 10:
    return gui_i18n_text(language, "tutorial.step.body.10");
  case 11:
    return gui_i18n_text(language, "tutorial.step.body.11");
  case 12:
    return gui_i18n_text(language, "tutorial.step.body.12");
  case 13:
    return gui_i18n_text(language, "tutorial.step.body.13");
  case 14:
  default:
    return gui_i18n_text(language, "tutorial.step.body.14");
  }
}
