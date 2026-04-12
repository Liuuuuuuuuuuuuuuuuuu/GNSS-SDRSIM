#ifndef MAP_CONTROL_PANEL_TEXT_UTILS_H
#define MAP_CONTROL_PANEL_TEXT_UTILS_H

#include <QString>

QString map_control_short_base_name(const QString &path);

QString map_control_compact_rnx_suffix_text(const QString &name);

QString map_control_week_sow_compact(int week, double sow);

#endif
