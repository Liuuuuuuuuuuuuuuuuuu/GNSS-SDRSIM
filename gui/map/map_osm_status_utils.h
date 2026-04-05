#ifndef MAP_OSM_STATUS_UTILS_H
#define MAP_OSM_STATUS_UTILS_H

#include <QPainter>
#include <QRect>
#include <QString>
#include <QStringList>

#include "gui/core/gui_i18n.h"

QString map_osm_current_text(bool receiver_valid, double receiver_lat,
                             double receiver_lon, GuiLanguage language);
QString map_osm_llh_text(bool has_selected_llh, double selected_lat,
                         double selected_lon, double selected_h,
                         const QString &current_text,
                         GuiLanguage language);
QString map_osm_zoom_text(int zoom, int zoom_base, GuiLanguage language);
QStringList map_osm_status_lines(const QString &zoom_text,
                                 bool tutorial_enabled,
                                 bool tutorial_overlay_visible,
                                 const QString &status_text,
                                 const QString &llh_text,
                                 GuiLanguage language);
void map_osm_draw_status_badges(QPainter &p, const QRect &panel,
                                const QRect &stop_btn_rect, bool running_ui,
                                const QStringList &lines);

#endif