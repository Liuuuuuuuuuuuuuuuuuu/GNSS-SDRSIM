#ifndef GUI_I18N_H
#define GUI_I18N_H

#include <QString>

enum class GuiLanguage : int {
  English = 0,
  ZhTw = 1,
};

GuiLanguage gui_language_from_locale_name(const QString &locale_name);
bool gui_language_is_zh_tw(GuiLanguage language);
QString gui_i18n_text(GuiLanguage language, const char *key);

#endif
