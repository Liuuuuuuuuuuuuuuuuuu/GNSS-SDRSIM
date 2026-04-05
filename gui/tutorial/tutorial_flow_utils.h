#ifndef TUTORIAL_FLOW_UTILS_H
#define TUTORIAL_FLOW_UTILS_H

#include <QString>

int tutorial_last_step();
int tutorial_spotlight_count_for_step(int step);
QString tutorial_step_title(int step);
QString tutorial_step_body(int step, bool detailed, bool running_ui);

#endif