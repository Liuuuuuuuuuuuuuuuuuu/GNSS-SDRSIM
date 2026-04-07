// gui_font_manager.h — Single source of truth for all GUI fonts.
// To change any font in the application, edit ONLY this file.
//
// Language rules:
//   Chinese (ZH-TW):  CJK glyphs  → Microsoft JhengHei (微軟正黑體)
//                     Latin glyphs → Times New Roman  (automatic Qt fallback)
//   English:          Everything   → Times New Roman
//
// Usage:
//   QFont f = gui_font_ui(in.language, 12);   // proportional UI / tutorial
//   QFont m = gui_font_mono(10);              // monospace data readouts
//   gui_font_apply_to_widget(widget, lang);   // apply to QWidget system font
//
#ifndef GUI_FONT_MANAGER_H
#define GUI_FONT_MANAGER_H

#include <QFont>
#include <QStringList>
#include <QWidget>
#include "gui/core/gui_i18n.h"

// ── Configurable family lists ─────────────────────────────────────────────────
// Primary CJK family (zh mode).  Microsoft JhengHei = 微軟正黑體.
#define GUI_FONT_ZH_PRIMARY      "Microsoft JhengHei"
// Latin fallback when CJK primary is active (Qt uses this for ASCII glyphs).
#define GUI_FONT_ZH_LATIN        "Times New Roman"
// System-level CJK fallback (if JhengHei not installed).
#define GUI_FONT_ZH_FALLBACK_CJK "Noto Sans CJK TC"
// Latin fallback chain (system-agnostic).
#define GUI_FONT_ZH_FALLBACK_LAT "Noto Sans"

// English mode primary.
#define GUI_FONT_EN_PRIMARY      "Times New Roman"
#define GUI_FONT_EN_FALLBACK_1   "Times New Roman PS MT"
#define GUI_FONT_EN_FALLBACK_2   "Nimbus Roman"
#define GUI_FONT_EN_FALLBACK_3   "Liberation Serif"
#define GUI_FONT_EN_FALLBACK_4   "Noto Serif"

// Monospace (language-independent, for data / numeric readouts).
#define GUI_FONT_MONO_PRIMARY    "Monospace"

// ── Inline factory functions ──────────────────────────────────────────────────

// Proportional UI font — use for all major text, tutorial overlays, buttons.
inline QFont gui_font_ui(GuiLanguage lang, int point_size = -1)
{
    QFont f;
    if (gui_language_is_zh_tw(lang)) {
        f.setFamilies(QStringList()
                      << GUI_FONT_ZH_PRIMARY
                      << GUI_FONT_ZH_LATIN
                      << GUI_FONT_ZH_FALLBACK_CJK
                      << GUI_FONT_ZH_FALLBACK_LAT);
        f.setStyleHint(QFont::SansSerif);
    } else {
        f.setFamilies(QStringList()
                      << GUI_FONT_EN_PRIMARY
                      << GUI_FONT_EN_FALLBACK_1
                      << GUI_FONT_EN_FALLBACK_2
                      << GUI_FONT_EN_FALLBACK_3
                      << GUI_FONT_EN_FALLBACK_4);
        f.setStyleHint(QFont::Serif);
    }
    if (point_size > 0) f.setPointSize(point_size);
    return f;
}

// Bold variant.
inline QFont gui_font_ui_bold(GuiLanguage lang, int point_size = -1)
{
    QFont f = gui_font_ui(lang, point_size);
    f.setBold(true);
    return f;
}

// Monospace — for numeric/hex readouts; independent of language.
inline QFont gui_font_mono(int point_size = -1)
{
    QFont f;
    f.setFamily(GUI_FONT_MONO_PRIMARY);
    f.setStyleHint(QFont::Monospace);
    if (point_size > 0) f.setPointSize(point_size);
    return f;
}

// Apply the language font to an entire QWidget (system font).
// Call this whenever the user changes the language setting.
inline void gui_font_apply_to_widget(QWidget *widget, GuiLanguage lang)
{
    if (!widget) return;

    auto apply_one = [lang](QWidget *w) {
        if (!w) return;
        QFont f = gui_font_ui(lang);
        int pt_sz = w->font().pointSize();
        if (pt_sz > 0) f.setPointSize(pt_sz); // preserve per-widget sizing only if valid
        f.setBold(w->font().bold());
        w->setFont(f);
    };

    apply_one(widget);
    const auto children = widget->findChildren<QWidget*>();
    for (QWidget *child : children) {
        apply_one(child);
    }
}

#endif // GUI_FONT_MANAGER_H
