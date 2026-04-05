#include "gui/tutorial/tutorial_flow_utils.h"

namespace {

const char *kStepTitles[] = {
    "Welcome",
    "Step 1: Pick Start Point",
    "Step 2: SIMPLE Tab",
    "Step 3: SIMPLE Control Panel",
    "Step 4: GNSS-SDRSIM (Top-Right)",
    "Step 5: Switch to DETAIL",
    "Step 6: DETAIL Control Panel",
    "Step 7: More DETAIL Controls",
    "Step 8: Start Transmission",
    "Step 9: Spectrum Panel",
    "Step 10: Waterfall Panel",
    "Step 11: Time-Domain Panel",
    "Step 12: Constellation Panel",
    "Step 13: Stop and Reset",
    "Step 14: Exit"};

const int kStepCount = (int)(sizeof(kStepTitles) / sizeof(kStepTitles[0]));

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

QString tutorial_step_title(int step) {
  return QString::fromUtf8(kStepTitles[clamp_step(step)]);
}

QString tutorial_step_body(int step, bool detailed, bool running_ui) {
  switch (clamp_step(step)) {
  case 0:
    return "Guide is optional and OFF by default.\nUse GUIDE OFF / GUIDE ON to show or hide this overlay.";
  case 1:
    return "Left map: left-click to set Start LLH.\nSTART becomes available after a valid start point.";
  case 2:
    return "SIMPLE tab: beginner page.\nUse this first for the fastest path to START.";
  case 3:
    return "SIMPLE controls: SYS, Fs, Tx Gain.\nHighlight shows each control in order.\nSYS and Fs must be compatible.";
  case 4:
    return "Top-right GNSS-SDRSIM panel: shows satellite geometry and receiver marker.";
  case 5:
    return "Switch to DETAIL tab for advanced controls.";
  case 6:
    return "DETAIL numeric controls: Gain, CN0, PRN, Seed, CH.\nHighlight cycles through them.";
  case 7:
    return detailed
               ? QString("DETAIL switches: MODE, FORMAT, MEO, IONO, EXT CLK.\nUse these for behavior and hardware options.")
               : QString("Component: extra DETAIL controls.\nDETAIL gives you more operations than SIMPLE.\nExamples: CH, CN0, PRN, and Seed.");
  case 8:
    return running_ui
               ? QString("You are already running.\nCurrent component highlighted: STOP SIMULATION button.\nUse it to stop current transmission.")
               : QString("Press START to begin generation/transmission.");
  case 9:
    return "Spectrum panel: power over frequency.";
  case 10:
    return "Waterfall panel: frequency history over time.";
  case 11:
    return "Time-domain panel: waveform over time.";
  case 12:
    return "Constellation panel: I/Q distribution and stability.";
  case 13:
    return "Press STOP SIMULATION to abort run and return to standby.";
  case 14:
  default:
    return "Press EXIT to close the program.";
  }
}
