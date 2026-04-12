#include "gui/map/render/map_fallback_land.h"

#include "gui/geo/geo_io.h"
#include "gui/map/render/map_render_utils.h"

namespace {

const LonLat kPolyNorthAmerica[] = {
    {-168, 72}, {-150, 70}, {-140, 60}, {-130, 55}, {-124, 48}, {-123, 40},
    {-117, 32}, {-108, 26}, {-97, 20},  {-88, 18},  {-82, 24},  {-81, 30},
    {-90, 45},  {-110, 60}, {-135, 70}, {-160, 74}};

const LonLat kPolySouthAmerica[] = {
    {-81, 12},  {-75, 7},   {-70, -5},  {-66, -15}, {-64, -25},
    {-62, -35}, {-58, -45}, {-52, -54}, {-45, -52}, {-41, -40},
    {-38, -25}, {-46, -8},  {-55, 2},   {-67, 10}};

const LonLat kPolyEurasia[] = {
    {-10, 35}, {0, 45},   {20, 55},  {40, 60},  {60, 62},  {80, 65},
    {100, 62}, {125, 58}, {145, 50}, {160, 48}, {170, 60}, {175, 68},
    {150, 72}, {120, 72}, {90, 70},  {65, 68},  {45, 60},  {30, 50},
    {20, 40},  {12, 35},  {5, 38},   {-5, 40}};

const LonLat kPolyAfrica[] = {{-17, 37}, {-6, 35},  {10, 35},  {22, 32},
                               {33, 25},  {41, 12},  {44, 3},   {40, -8},
                               {34, -20}, {27, -30}, {15, -35}, {5, -34},
                               {-3, -22}, {-9, -5},  {-15, 10}};

const LonLat kPolyAustralia[] = {
    {112, -11}, {123, -16}, {134, -18}, {146, -22}, {153, -29},
    {150, -38}, {141, -40}, {130, -35}, {118, -30}, {113, -22}};

const LonLat kPolyGreenland[] = {{-73, 60}, {-60, 65}, {-48, 70},
                                  {-40, 76}, {-42, 82}, {-55, 84},
                                  {-65, 80}, {-70, 72}};

} // namespace

void map_draw_fallback_land(QPainter &p, const QRect &map_rect) {
  map_draw_poly(
      p, kPolyNorthAmerica,
      (int)(sizeof(kPolyNorthAmerica) / sizeof(kPolyNorthAmerica[0])),
      map_rect);
  map_draw_poly(
      p, kPolySouthAmerica,
      (int)(sizeof(kPolySouthAmerica) / sizeof(kPolySouthAmerica[0])),
      map_rect);
  map_draw_poly(p, kPolyEurasia,
                (int)(sizeof(kPolyEurasia) / sizeof(kPolyEurasia[0])),
                map_rect);
  map_draw_poly(p, kPolyAfrica,
                (int)(sizeof(kPolyAfrica) / sizeof(kPolyAfrica[0])), map_rect);
  map_draw_poly(p, kPolyAustralia,
                (int)(sizeof(kPolyAustralia) / sizeof(kPolyAustralia[0])),
                map_rect);
  map_draw_poly(p, kPolyGreenland,
                (int)(sizeof(kPolyGreenland) / sizeof(kPolyGreenland[0])),
                map_rect);
}
