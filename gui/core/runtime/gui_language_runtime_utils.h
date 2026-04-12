#ifndef GUI_LANGUAGE_RUNTIME_UTILS_H
#define GUI_LANGUAGE_RUNTIME_UTILS_H

#include <QString>

#include "gui/core/i18n/gui_i18n.h"

class QLineEdit;
class QWidget;

GuiLanguage gui_runtime_default_language();
GuiLanguage gui_runtime_toggle_language(GuiLanguage language);
QString gui_runtime_text(GuiLanguage language, const char *key);
void gui_runtime_apply_search_placeholder(QLineEdit *search_box,
                                          GuiLanguage language);
void gui_runtime_load_bundled_fonts();
void gui_runtime_apply_optional_times_fonts(bool load_bold, bool load_italic, bool load_bold_italic);
void gui_runtime_apply_language_font(QWidget *widget, GuiLanguage language);

#endif
