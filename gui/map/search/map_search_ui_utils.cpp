#include "gui/map/search/map_search_ui_utils.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QSize>

void map_search_hide_results(QListWidget *list) {
  if (!list)
    return;
  list->clear();
  list->setVisible(false);
}

QListWidgetItem *map_search_current_or_first(QListWidget *list) {
  if (!list || !list->isVisible())
    return nullptr;

  QListWidgetItem *current = list->currentItem();
  if (!current && list->count() > 0) {
    list->setCurrentRow(0);
    current = list->currentItem();
  }
  return current;
}

int map_search_populate_results(QListWidget *list,
                                const std::vector<MapSearchResult> &results) {
  if (!list)
    return 0;

  // Reduce per-item repaint/signals while repopulating search results.
  const QSignalBlocker blocker(list);
  list->setUpdatesEnabled(false);
  list->clear();
  for (const auto &r : results) {
    QListWidgetItem *item = new QListWidgetItem(r.line, list);
    item->setSizeHint(QSize(item->sizeHint().width(), 44));
    item->setData(Qt::UserRole, r.lat);
    item->setData(Qt::UserRole + 1, r.lon);
    item->setData(Qt::UserRole + 2, r.display_name);
    item->setData(Qt::UserRole + 3, r.primary_text);
    item->setData(Qt::UserRole + 4, r.secondary_text);
    item->setData(Qt::UserRole + 5, r.ranking_score);
  }
  list->setUpdatesEnabled(true);

  return list->count();
}
