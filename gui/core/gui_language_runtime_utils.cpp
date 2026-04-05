#include "gui/core/gui_language_runtime_utils.h"

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
  const QStringList candidates = {
      QString("./fonts/kaiu.ttf"),
      QString("./fonts/times.ttf"),
      QString("./fonts/timesbd.ttf"),
      QString("./fonts/timesi.ttf"),
      QString("./fonts/timesbi.ttf")};
  for (const QString &path : candidates) {
    try_add_font_file(path);
  }
}

void gui_runtime_apply_language_font(QWidget *widget, GuiLanguage language) {
  if (!widget) {
    return;
  }

  QFont f = widget->font();
  if (gui_language_is_zh_tw(language)) {
    f.setFamilies(QStringList() << "DFKai-SB"
                                << "標楷體"
                                << "KaiU"
                                << "BiauKai"
                                << "KaiTi"
                                << "Noto Serif CJK TC"
                                << "Noto Sans CJK TC");
  } else {
    f.setFamilies(QStringList() << "Times New Roman"
                                << "Times New Roman PS MT"
                                << "Nimbus Roman"
                                << "Liberation Serif");
  }
  f.setStyleHint(QFont::Serif);
  widget->setFont(f);
}
