// gui_font_manager.h — Single source of truth for all GUI fonts.
// To change any font in the application, edit ONLY this file.
//
// Language rules:
//   Chinese (ZH-TW):  Latin glyphs  → Arial
//                     CJK glyphs    → Microsoft JhengHei (default) or BiauKai
//   English:          Everything    → Arial
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
// Optional CJK family for zh mode when Kai style is enabled.
#define GUI_FONT_ZH_KAI_PRIMARY  "DFKai-SB"
// Latin primary for all English glyphs.
#define GUI_FONT_ZH_LATIN        "Arial"
// System-level CJK fallback (if JhengHei not installed).
#define GUI_FONT_ZH_FALLBACK_CJK "Noto Sans CJK TC"
// Latin fallback chain (system-agnostic).
#define GUI_FONT_ZH_FALLBACK_LAT "Noto Sans"

// English mode primary.
#define GUI_FONT_EN_PRIMARY      "Arial"
#define GUI_FONT_EN_FALLBACK_1   "Arial MT"
#define GUI_FONT_EN_FALLBACK_2   "Liberation Sans"
#define GUI_FONT_EN_FALLBACK_3   "Helvetica"
#define GUI_FONT_EN_FALLBACK_4   "Noto Sans"

// Monospace (language-independent, for data / numeric readouts).
#define GUI_FONT_MONO_PRIMARY    "Monospace"

// ── Inline factory functions ──────────────────────────────────────────────────

inline bool &gui_font_zh_kai_enabled_ref()
{
    static bool enabled = false;
    return enabled;
}

inline void gui_font_set_zh_kai_enabled(bool enabled)
{
    gui_font_zh_kai_enabled_ref() = enabled;
}

inline bool gui_font_zh_kai_enabled()
{
    return gui_font_zh_kai_enabled_ref();
}

// Proportional UI font — use for all major text, tutorial overlays, buttons.
inline QFont gui_font_ui(GuiLanguage lang, int point_size = -1)
{
    QFont f;
    if (gui_language_is_zh_tw(lang)) {
        const char *cjk_primary =
            gui_font_zh_kai_enabled() ? GUI_FONT_ZH_KAI_PRIMARY : GUI_FONT_ZH_PRIMARY;
        f.setFamilies(QStringList()
                      << GUI_FONT_ZH_LATIN
                      << cjk_primary
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
        f.setStyleHint(QFont::SansSerif);
    }
    if (point_size > 0) f.setPointSize(point_size);
    return f;
}

// Bold variant (kept for API compat; no longer renders bold).
inline QFont gui_font_ui_bold(GuiLanguage lang, int point_size = -1)
{
    QFont f = gui_font_ui(lang, point_size);
    return f;
}

// Numeric/readout font profile.
// English glyphs are still forced to Arial per global rule.
inline QFont gui_font_mono(int point_size = -1)
{
    QFont f;
    const char *cjk_primary =
        gui_font_zh_kai_enabled() ? GUI_FONT_ZH_KAI_PRIMARY : GUI_FONT_ZH_PRIMARY;
    f.setFamilies(QStringList()
                  << GUI_FONT_EN_PRIMARY
                  << GUI_FONT_EN_FALLBACK_1
                  << GUI_FONT_EN_FALLBACK_2
                  << GUI_FONT_EN_FALLBACK_3
                  << GUI_FONT_EN_FALLBACK_4
                  << cjk_primary
                  << GUI_FONT_ZH_FALLBACK_CJK
                  << GUI_FONT_ZH_FALLBACK_LAT);
    f.setStyleHint(QFont::SansSerif);
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
