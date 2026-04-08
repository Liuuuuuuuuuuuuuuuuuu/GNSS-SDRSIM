#include "gui/core/gui_language_runtime_utils.h"
#include "gui/core/gui_font_manager.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QLineEdit>
#include <QLocale>
#include <QWidget>

namespace {

void try_add_font_file(const QString &path) {
  if (!QFileInfo::exists(path)) {
    return;
  }
  QFontDatabase::addApplicationFont(path);
}

} // namespace

GuiLanguage gui_runtime_default_language() {
  return gui_language_from_locale_name(QLocale::system().name().replace('_', '-'));
}

GuiLanguage gui_runtime_toggle_language(GuiLanguage language) {
  return gui_language_is_zh_tw(language) ? GuiLanguage::English : GuiLanguage::ZhTw;
}

QString gui_runtime_text(GuiLanguage language, const char *key) {
  return gui_i18n_text(language, key);
}

void gui_runtime_apply_search_placeholder(QLineEdit *search_box,
                                          GuiLanguage language) {
  if (!search_box) {
    return;
  }
  search_box->setPlaceholderText(gui_runtime_text(language, "search.placeholder"));
}

void gui_runtime_load_bundled_fonts() {
  // Load only the required base fonts; optional variants are controlled separately.
  const QStringList required = {
      QString("./fonts/kaiu.ttf"),
      QString("./fonts/times.ttf")};
  for (const QString &path : required) {
    try_add_font_file(path);
  }
}

namespace {
  int s_font_id_timesbd = -1;
  int s_font_id_timesi  = -1;
  int s_font_id_timesbi = -1;
} // namespace

void gui_runtime_apply_optional_times_fonts(bool load_bold, bool load_italic,
                                            bool load_bold_italic) {
  // Bold
  if (!load_bold && s_font_id_timesbd >= 0) {
    QFontDatabase::removeApplicationFont(s_font_id_timesbd);
    s_font_id_timesbd = -1;
  } else if (load_bold && s_font_id_timesbd < 0) {
    const QString path("./fonts/timesbd.ttf");
    if (QFileInfo::exists(path))
      s_font_id_timesbd = QFontDatabase::addApplicationFont(path);
  }
  // Italic
  if (!load_italic && s_font_id_timesi >= 0) {
    QFontDatabase::removeApplicationFont(s_font_id_timesi);
    s_font_id_timesi = -1;
  } else if (load_italic && s_font_id_timesi < 0) {
    const QString path("./fonts/timesi.ttf");
    if (QFileInfo::exists(path))
      s_font_id_timesi = QFontDatabase::addApplicationFont(path);
  }
  // Bold Italic
  if (!load_bold_italic && s_font_id_timesbi >= 0) {
    QFontDatabase::removeApplicationFont(s_font_id_timesbi);
    s_font_id_timesbi = -1;
  } else if (load_bold_italic && s_font_id_timesbi < 0) {
    const QString path("./fonts/timesbi.ttf");
    if (QFileInfo::exists(path))
      s_font_id_timesbi = QFontDatabase::addApplicationFont(path);
  }
}

void gui_runtime_apply_language_font(QWidget *widget, GuiLanguage language) {
  // Keep a global app font in sync so newly created widgets do not keep stale
  // families after language toggles (EN -> ZH -> EN).
  QApplication::setFont(gui_font_ui(language));

  // Delegate to the centralized font manager so one file controls all fonts.
  gui_font_apply_to_widget(widget, language);
}
