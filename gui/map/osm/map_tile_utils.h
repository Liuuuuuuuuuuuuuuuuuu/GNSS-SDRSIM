#ifndef MAP_TILE_UTILS_H
#define MAP_TILE_UTILS_H

#include <QPixmap>
#include <QString>

#include <unordered_map>
#include <vector>

// Increased cache size to support seamless tile prefetching.
// 480 tiles ≈ 120MB (256px × 256px × 4 bytes/pixel × 480)
// Allows prefetching up to 3 rings (121 visible + 288 adjacent + 71 extended)
static constexpr int kMaxTileCacheEntries = 480;

struct MapTileRange {
  int n = 1;
  int tx0 = 0;
  int tx1 = 0;
  int ty0 = 0;
  int ty1 = 0;
  double left = 0.0;
  double top = 0.0;
};

MapTileRange map_tile_visible_range(int zoom, double center_x, double center_y,
                                    int panel_w, int panel_h);
int map_tile_wrap_x(int tx, int n);
QString map_tile_key(int zoom, int tx, int ty, bool dark_map_mode);
QString map_tile_url(int zoom, int tx_wrap, int ty, bool dark_map_mode);
void map_tile_store_cache_item(
  std::unordered_map<QString, QPixmap> *cache,
  std::vector<QString> *order, const QString &key, QPixmap &&pixmap,
  int max_entries = kMaxTileCacheEntries);
void map_tile_trim_cache(std::unordered_map<QString, QPixmap> *cache,
             std::vector<QString> *order, int max_entries);

#endif