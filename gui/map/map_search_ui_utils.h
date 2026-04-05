#ifndef MAP_SEARCH_UI_UTILS_H
#define MAP_SEARCH_UI_UTILS_H

#include "gui/map/map_search_utils.h"

class QListWidget;
class QListWidgetItem;

void map_search_hide_results(QListWidget *list);
QListWidgetItem *map_search_current_or_first(QListWidget *list);
int map_search_populate_results(QListWidget *list,
                                const std::vector<MapSearchResult> &results);

#endif