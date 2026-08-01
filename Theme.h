// ─────────────────────────────────────────────────────────────────────────────
// Theme.cpp  –  OnlyFrags Com
// ─────────────────────────────────────────────────────────────────────────────
#include "Theme.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

// ── Static members ────────────────────────────────────────────────────────────
bool       Theme::s_dark    = false;
ThemeMode  Theme::s_mode    = ThemeMode::Auto;
Palette    Theme::s_palette = {};
ThemeCache Theme::s_cache   = {};

// ── Dark palette ──────────────────────────────────────────────────────────────
static const Palette DARK_PALETTE = {
    /* bg_main       */ RGB( 30,  30,  36),
    /* bg_secondary  */ RGB( 40,  40,  48),
    /* bg_input      */ RGB( 50,  50,  60),
    /* bg_hover      */ RGB( 50,  50,  62),
    /* bg_selected   */ RGB( 60,  90, 160),
    /* bg_button     */ RGB( 55,  55,  65),
    /* bg_button_hov */ RGB( 75,  75,  90),
    /* bg_mute       */ RGB(180,  50,  50),
    /* bg_speaking   */ RGB( 30, 120,  60),
    /* fg_primary    */ RGB(230, 230, 235),
    /* fg_secondary  */ RGB(130, 130, 150),
    /* fg_button     */ RGB(220, 220, 225),
    /* accent        */ RGB( 88, 130, 220),
    /* border        */ RGB( 60,  60,  72),
};

// ── Light palette ─────────────────────────────────────────────────────────────
static const Palette LIGHT_PALETTE = {
    /* bg_main       */ RGB(242, 243, 247),
    /* bg_secondary  */ RGB(255, 255, 255),
    /* bg_input      */ RGB(255, 255, 255),
    /* bg_hover      */ RGB(230, 232, 240),
    /* bg_selected   */ RGB(200, 215, 245),
    /* bg_button     */ RGB(220, 222, 230),
    /* bg_button_hov */ RGB(200, 205, 220),
    /* bg_mute       */ RGB(200,  60,  60),
    /* bg_speaking   */ RGB( 40, 160,  80),
    /* fg_primary    */ RGB( 20,  20,  25),
    /* fg_secondary  */ RGB( 90,  90, 110),
    /* fg_button     */ RGB( 20,  20,  30),
    /* accent        */ RGB( 60, 100, 200),
    /* border        */ RGB(200, 200, 215),
};

// ── ThemeCache ────────────────────────────────────────────────────────────────
void ThemeCache::Release() {
    auto del = [](HBRUSH& b){ if (b) { DeleteObject(b); b = nullptr; } };
    del(br_main);    del(br_secondary); del(br_input);
    del(br_hover);   del(br_selected);  del(br_button);
    del(br_button_hov); del(br_mute);  del(br_speaking);
    if (pen_border) { DeleteObject(pen_border); pen_border = nullptr; }
}

void ThemeCache::Build(const Palette& p) {
    Release();
    br_main        = CreateSolidBrush(p.bg_main);
    br_secondary   = CreateSolidBrush(p.bg_secondary);
    br_input       = CreateSolidBrush(p.bg_input);
    br_hover       = CreateSolidBrush(p.bg_hover);
    br_selected    = CreateSolidBrush(p.bg_selected);
    br_button      = CreateSolidBrush(p.bg_button);
    br_button_hov  = CreateSolidBrush(p.bg_button_hov);
    br_mute        = CreateSolidBrush(p.bg_mute);
    br_speaking    = CreateSolidBrush(p.bg_speaking);
    pen_border     = CreatePen(PS_SOLID, 1, p.border);
}

// ── Theme API ─────────────────────────────────────────────────────────────────
bool Theme::SystemPrefersDark() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD value = 1;
    DWORD size  = sizeof(value);
    RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr,
                     nullptr, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(hKey);
    return (value == 0);   // 0 = dark, 1 = light
}

void Theme::Refresh(ThemeMode mode) {
    s_mode = mode;
    switch (mode) {
        case ThemeMode::Dark:  s_dark = true;                  break;
        case ThemeMode::Light: s_dark = false;                 break;
        default:               s_dark = SystemPrefersDark();   break;
    }
    s_palette = s_dark ? DARK_PALETTE : LIGHT_PALETTE;
    s_cache.Build(s_palette);
}

void Theme::Init(ThemeMode mode) {
    Refresh(mode);
}

bool Theme::IsDark() { return s_dark; }

const Palette& Theme::Colors() { return s_palette; }

const ThemeCache& Theme::Cache() { return s_cache; }

void Theme::ApplyTitleBar(HWND hwnd) {
    if (!hwnd) return;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    BOOL dark = s_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                          &dark, sizeof(dark));
}
