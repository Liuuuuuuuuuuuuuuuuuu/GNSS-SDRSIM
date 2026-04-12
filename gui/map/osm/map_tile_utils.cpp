#include "gui/map/osm/map_tile_utils.h"

#include <cmath>

void map_tile_trim_cache(std::unordered_map<QString, QPixmap> *cache,
                         std::vector<QString> *order, int max_entries) {
  if (!cache || !order)
    return;
  if (max_entries < 1)
    max_entries = 1;
  while ((int)order->size() > max_entries) {
    const QString key = order->front();
    order->erase(order->begin());
    cache->erase(key);
  }
}

void map_tile_store_cache_item(
    std::unordered_map<QString, QPixmap> *cache,
    std::vector<QString> *order, const QString &key, QPixmap &&pixmap,
    int max_entries) {
  if (!cache || !order)
    return;

  auto it = cache->find(key);
  if (it == cache->end()) {
    order->push_back(key);
  }
  (*cache)[key] = std::move(pixmap);
  map_tile_trim_cache(cache, order, max_entries);
}

MapTileRange map_tile_visible_range(int zoom, double center_x, double center_y,
                                    int panel_w, int panel_h) {
  MapTileRange r;
  r.n = 1 << zoom;
  r.left = center_x - (double)panel_w * 0.5;
  r.top = center_y - (double)panel_h * 0.5;
  r.tx0 = (int)std::floor(r.left / 256.0);
  r.tx1 = (int)std::floor((r.left + panel_w) / 256.0);
  r.ty0 = (int)std::floor(r.top / 256.0);
  r.ty1 = (int)std::floor((r.top + panel_h) / 256.0);
  return r;
}

int map_tile_wrap_x(int tx, int n) {
  int tx_wrap = tx % n;
  if (tx_wrap < 0)
    tx_wrap += n;
  return tx_wrap;
}

QString map_tile_key(int zoom, int tx, int ty, bool dark_map_mode) {
  return QString("%1/%2/%3/%4")
      .arg(zoom)
      .arg(tx)
      .arg(ty)
      .arg(dark_map_mode ? 1 : 0);
}

QString map_tile_url(int zoom, int tx_wrap, int ty, bool dark_map_mode) {
  if (dark_map_mode) {
    return QString("https://tile.openstreetmap.org/%1/%2/%3.png")
        .arg(zoom)
        .arg(tx_wrap)
        .arg(ty);
  }
  return QString("https://mt1.google.com/vt/lyrs=y&x=%1&y=%2&z=%3")
      .arg(tx_wrap)
      .arg(ty)
      .arg(zoom);
}
