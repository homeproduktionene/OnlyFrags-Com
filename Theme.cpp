#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SettingsWindow.h  –  OnlyFrags Com
// Modal settings dialog with tabs: Allgemein / Audio / Darstellung / Netzwerk
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include "Config.h"

class SettingsWindow {
public:
    // Opens the settings dialog modally. Blocks until user closes it.
    // Returns true if settings were changed and saved.
    static bool Open(HINSTANCE hInst, HWND parent, AppConfig& cfg);

private:
    struct State {
        AppConfig*  cfg    = nullptr;
        AppConfig   draft;          // working copy, applied only on OK
        HWND        tab    = nullptr;
        HWND        pages[4] = {};  // one child HWND per tab page
        int         cur_tab = 0;
        HFONT       font   = nullptr;
        bool        saved  = false;
    };

    enum TabIndex { TAB_GENERAL = 0, TAB_AUDIO, TAB_APPEARANCE, TAB_NETWORK };

    // Control ID ranges per tab (1000*tab + offset)
    enum IDs : int {
        // General
        ID_GEN_LANG          = 1000,
        ID_GEN_AUTOSTART     = 1001,
        ID_GEN_MINIMIZED     = 1002,
        ID_GEN_NOTIFY        = 1003,

        // Audio
        ID_AUD_MIC_COMBO     = 2000,
        ID_AUD_OUT_COMBO     = 2001,
        ID_AUD_MIC_VOL       = 2002,
        ID_AUD_OUT_VOL       = 2003,
        ID_AUD_BITRATE       = 2004,
        ID_AUD_PTT           = 2005,
        ID_AUD_VAD           = 2006,
        ID_AUD_VAD_THRESH    = 2007,
        ID_AUD_ECHO          = 2008,
        ID_AUD_NOISE         = 2009,
        ID_AUD_AGC           = 2010,
        ID_AUD_MIC_TEST      = 2011,

        // Appearance
        ID_APP_AUTO          = 3000,
        ID_APP_LIGHT         = 3001,
        ID_APP_DARK          = 3002,

        // Network
        ID_NET_HOST          = 4000,
        ID_NET_TCP_PORT      = 4001,
        ID_NET_UDP_PORT      = 4002,
        ID_NET_VERIFY_TLS    = 4003,

        // Dialog buttons
        ID_OK                = 5000,
        ID_CANCEL            = 5001,
        ID_APPLY             = 5002,
    };

    static constexpr wchar_t DLG_CLASS[]  = L"OFC_Settings";
    static constexpr wchar_t PAGE_CLASS[] = L"OFC_SettingsPage";

    static State* GetState(HWND hwnd) {
        return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    static LRESULT CALLBACK DlgProc (HWND, UINT, WPARAM, LPARAM);

    static HWND BuildPageGeneral   (HWND parent, State* st);
    static HWND BuildPageAudio     (HWND parent, State* st);
    static HWND BuildPageAppearance(HWND parent, State* st);
    static HWND BuildPageNetwork   (HWND parent, State* st);

    static void ShowTab    (State* st, int idx);
    static void LoadDraft  (State* st);     // UI → draft
    static void ApplyDraft (State* st);     // draft → cfg + save
    static void ThemeControls(HWND hwnd);
};
