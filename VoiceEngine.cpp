#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Theme.h  –  OnlyFrags Com
// Dark / Light colour scheme, Windows dark-mode detection.
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "Config.h"

// ── Colour palette ────────────────────────────────────────────────────────────
struct Palette {
    COLORREF bg_main;        // window / sidebar background
    COLORREF bg_secondary;   // chat area, list items
    COLORREF bg_input;       // text input fields
    COLORREF bg_hover;       // hover highlight
    COLORREF bg_selected;    // selected list item
    COLORREF bg_button;      // normal button
    COLORREF bg_button_hov;  // button hover
    COLORREF bg_mute;        // mute-button active
    COLORREF bg_speaking;    // speaking user highlight
    COLORREF fg_primary;     // main text
    COLORREF fg_secondary;   // dimmed / timestamp text
    COLORREF fg_button;      // button text
    COLORREF accent;         // accent / focus ring
    COLORREF border;         // separator lines
};

// ── Cached brushes / pens (rebuilt on theme change) ──────────────────────────
struct ThemeCache {
    HBRUSH br_main        = nullptr;
    HBRUSH br_secondary   = nullptr;
    HBRUSH br_input       = nullptr;
    HBRUSH br_hover       = nullptr;
    HBRUSH br_selected    = nullptr;
    HBRUSH br_button      = nullptr;
    HBRUSH br_button_hov  = nullptr;
    HBRUSH br_mute        = nullptr;
    HBRUSH br_speaking    = nullptr;
    HPEN   pen_border     = nullptr;

    void   Release();
    void   Build(const Palette& p);
};

// ── Theme singleton ───────────────────────────────────────────────────────────
class Theme {
public:
    // Call once after creating the first window
    static void     Init(ThemeMode mode = ThemeMode::Auto);

    // Returns true if the current effective mode is dark
    static bool     IsDark();

    // Returns the active palette
    static const Palette&     Colors();
    static const ThemeCache&  Cache();

    // Re-evaluate (call on WM_SETTINGCHANGE or manual override)
    static void     Refresh(ThemeMode mode = ThemeMode::Auto);

    // Apply dark-mode chrome to a window (Windows 10 1903+)
    static void     ApplyTitleBar(HWND hwnd);

    // Detect Windows dark-mode preference from registry
    static bool     SystemPrefersDark();

private:
    static bool       s_dark;
    static ThemeMode  s_mode;
    static Palette    s_palette;
    static ThemeCache s_cache;
};
