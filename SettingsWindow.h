// ─────────────────────────────────────────────────────────────────────────────
// SettingsWindow.cpp  –  OnlyFrags Com
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <string>
#include <sstream>
#pragma comment(lib, "comctl32.lib")

#include "SettingsWindow.h"
#include "Theme.h"
#include "Config.h"
#include "VoiceEngine.h"

extern VoiceEngine* g_voice;

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::wstring GetDlgItemTextW_(HWND dlg, int id) {
    HWND hc = GetDlgItem(dlg, id);
    int  n  = GetWindowTextLengthW(hc) + 1;
    std::wstring s(n, L'\0');
    GetWindowTextW(hc, s.data(), n);
    s.resize(wcslen(s.c_str()));
    return s;
}
static std::string W2U(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static HWND MakeLabel(HWND parent, const wchar_t* text,
                      int x, int y, int w, int h, HFONT font) {
    HWND hc = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hc;
}
static HWND MakeCheck(HWND parent, const wchar_t* text,
                      int id, int x, int y, int w, int h,
                      bool checked, HFONT font) {
    HWND hc = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, h,
        parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(hc, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hc;
}
static HWND MakeEdit(HWND parent, int id, const wchar_t* text,
                     int x, int y, int w, int h, HFONT font,
                     DWORD extra = 0) {
    HWND hc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hc;
}
static HWND MakeCombo(HWND parent, int id,
                      int x, int y, int w, int h, HFONT font) {
    HWND hc = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hc;
}
static HWND MakeTrackbar(HWND parent, int id,
                         int x, int y, int w, int h,
                         int lo, int hi, int pos) {
    HWND hc = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, TBM_SETRANGE,  TRUE,  MAKELPARAM(lo, hi));
    SendMessageW(hc, TBM_SETPOS,    TRUE,  pos);
    return hc;
}
static HWND MakeRadio(HWND parent, const wchar_t* text,
                      int id, int x, int y, int w, int h,
                      bool checked, bool first, HFONT font) {
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON |
                  (first ? WS_GROUP : 0);
    HWND hc = CreateWindowExW(0, L"BUTTON", text, style,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (checked) SendMessageW(hc, BM_SETCHECK, BST_CHECKED, 0);
    return hc;
}
static HWND MakeButton(HWND parent, const wchar_t* text, int id,
                       int x, int y, int w, int h, HFONT font) {
    HWND hc = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return hc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page: Allgemein
// ─────────────────────────────────────────────────────────────────────────────
HWND SettingsWindow::BuildPageGeneral(HWND parent, State* st) {
    HWND pg = CreateWindowExW(0, PAGE_CLASS, L"",
        WS_CHILD, 0, 0, 1, 1, parent, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    HFONT f = st->font;

    MakeLabel  (pg, L"Sprache:",                  10,  14, 160, 18, f);
    HWND hLang = MakeCombo(pg, ID_GEN_LANG,        170, 10, 160, 120, f);
    SendMessageW(hLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Deutsch (de)"));
    SendMessageW(hLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English (en)"));
    int lang_sel = (st->draft.language == "en") ? 1 : 0;
    SendMessageW(hLang, CB_SETCURSEL, lang_sel, 0);

    MakeCheck(pg, L"Mit Windows starten",
              ID_GEN_AUTOSTART, 10, 52, 280, 22,
              st->draft.start_with_windows, f);
    MakeCheck(pg, L"Minimiert starten",
              ID_GEN_MINIMIZED,  10, 80, 280, 22,
              st->draft.start_minimized, f);
    MakeCheck(pg, L"Desktop-Benachrichtigungen",
              ID_GEN_NOTIFY,    10, 108, 280, 22,
              st->draft.notifications, f);
    return pg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page: Audio
// ─────────────────────────────────────────────────────────────────────────────
HWND SettingsWindow::BuildPageAudio(HWND parent, State* st) {
    HWND pg = CreateWindowExW(0, PAGE_CLASS, L"",
        WS_CHILD, 0, 0, 1, 1, parent, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    HFONT f = st->font;
    int y = 10;
    auto nl = [&](int dy = 30) { y += dy; };

    // Microphone
    MakeLabel(pg, L"Mikrofon:", 10, y, 130, 18, f);
    HWND hMic = MakeCombo(pg, ID_AUD_MIC_COMBO, 145, y - 2, 220, 200, f);
    SendMessageW(hMic, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Standard-Mikrofon"));
    if (g_voice) {
        for (const auto& dev : g_voice->EnumInputDevices())
            SendMessageW(hMic, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(dev.c_str()));
    }
    SendMessageW(hMic, CB_SETCURSEL, 0, 0);
    nl();

    // Lautsprecher
    MakeLabel(pg, L"Lautsprecher:", 10, y, 130, 18, f);
    HWND hOut = MakeCombo(pg, ID_AUD_OUT_COMBO, 145, y - 2, 220, 200, f);
    SendMessageW(hOut, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Standard-Lautsprecher"));
    if (g_voice) {
        for (const auto& dev : g_voice->EnumOutputDevices())
            SendMessageW(hOut, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(dev.c_str()));
    }
    SendMessageW(hOut, CB_SETCURSEL, 0, 0);
    nl();

    // Mic volume
    MakeLabel(pg, L"Mikrofonlautstärke:", 10, y, 150, 18, f);
    MakeTrackbar(pg, ID_AUD_MIC_VOL, 165, y, 200, 22,
                 0, 200, static_cast<int>(st->draft.mic_volume * 100.0f));
    nl();

    // Out volume
    MakeLabel(pg, L"Ausgabelautstärke:", 10, y, 150, 18, f);
    MakeTrackbar(pg, ID_AUD_OUT_VOL, 165, y, 200, 22,
                 0, 200, static_cast<int>(st->draft.out_volume * 100.0f));
    nl();

    // Bitrate
    MakeLabel(pg, L"Codec-Bitrate:", 10, y, 130, 18, f);
    HWND hBr = MakeCombo(pg, ID_AUD_BITRATE, 145, y - 2, 120, 200, f);
    const wchar_t* rates[] = { L"32 kbit/s", L"48 kbit/s",
                                L"64 kbit/s", L"96 kbit/s", L"128 kbit/s" };
    int rate_vals[] = { 32000, 48000, 64000, 96000, 128000 };
    int sel_rate = 2;
    for (int i = 0; i < 5; ++i) {
        SendMessageW(hBr, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(rates[i]));
        if (rate_vals[i] == st->draft.bitrate) sel_rate = i;
    }
    SendMessageW(hBr, CB_SETCURSEL, sel_rate, 0);
    nl();

    // Voice mode
    MakeLabel(pg, L"Spracheingabe:", 10, y, 130, 18, f);
    MakeRadio(pg, L"Sprachaktivierung", ID_AUD_VAD, 145, y, 150, 22,
              st->draft.voice_mode == VoiceMode::VoiceActivation, true, f);
    MakeRadio(pg, L"Push-to-Talk",      ID_AUD_PTT, 300, y, 130, 22,
              st->draft.voice_mode == VoiceMode::PushToTalk, false, f);
    nl();

    // VAD threshold
    MakeLabel(pg, L"Aktivierungsschwelle:", 10, y, 160, 18, f);
    MakeTrackbar(pg, ID_AUD_VAD_THRESH, 175, y, 190, 22,
                 0, 100, static_cast<int>(st->draft.vad_threshold * 100.0f));
    nl();

    // Echo / Noise / AGC
    MakeCheck(pg, L"Echounterdrückung",     ID_AUD_ECHO,  10, y, 200, 22,
              st->draft.echo_cancel,   f); nl();
    MakeCheck(pg, L"Geräuschunterdrückung", ID_AUD_NOISE, 10, y, 200, 22,
              st->draft.noise_suppress, f); nl();
    MakeCheck(pg, L"Automatische Verstärkungsregelung", ID_AUD_AGC, 10, y, 280, 22,
              st->draft.auto_gain, f); nl();

    // Mic test button
    MakeButton(pg, L"Mikrofontest starten", ID_AUD_MIC_TEST,
               10, y, 180, 28, f);

    return pg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page: Darstellung
// ─────────────────────────────────────────────────────────────────────────────
HWND SettingsWindow::BuildPageAppearance(HWND parent, State* st) {
    HWND pg = CreateWindowExW(0, PAGE_CLASS, L"",
        WS_CHILD, 0, 0, 1, 1, parent, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    HFONT f = st->font;

    MakeLabel(pg, L"Design:", 10, 14, 100, 18, f);
    MakeRadio(pg, L"Automatisch (Windows-Einstellung)",
              ID_APP_AUTO,  10, 40, 300, 22,
              st->draft.theme == ThemeMode::Auto, true, f);
    MakeRadio(pg, L"Hell",
              ID_APP_LIGHT, 10, 68, 300, 22,
              st->draft.theme == ThemeMode::Light, false, f);
    MakeRadio(pg, L"Dunkel",
              ID_APP_DARK,  10, 96, 300, 22,
              st->draft.theme == ThemeMode::Dark, false, f);
    return pg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page: Netzwerk
// ─────────────────────────────────────────────────────────────────────────────
HWND SettingsWindow::BuildPageNetwork(HWND parent, State* st) {
    HWND pg = CreateWindowExW(0, PAGE_CLASS, L"",
        WS_CHILD, 0, 0, 1, 1, parent, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    HFONT f = st->font;

    MakeLabel(pg, L"Serveradresse:", 10, 14, 140, 18, f);
    MakeEdit (pg, ID_NET_HOST, U2W(st->draft.host).c_str(),
              155, 10, 200, 24, f);

    MakeLabel(pg, L"TCP-Port:", 10, 48, 140, 18, f);
    MakeEdit (pg, ID_NET_TCP_PORT,
              std::to_wstring(st->draft.tcp_port).c_str(),
              155, 44, 80, 24, f, ES_NUMBER);

    MakeLabel(pg, L"UDP-Port (Voice):", 10, 82, 140, 18, f);
    MakeEdit (pg, ID_NET_UDP_PORT,
              std::to_wstring(st->draft.udp_port).c_str(),
              155, 78, 80, 24, f, ES_NUMBER);

    MakeCheck(pg, L"TLS-Zertifikat überprüfen (für echte CA-Certs)",
              ID_NET_VERIFY_TLS, 10, 116, 340, 22,
              st->draft.verify_tls, f);

    MakeLabel(pg, L"Hinweis: Adressänderungen wirken nach einem Neustart.",
              10, 150, 360, 36, f);
    return pg;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadDraft  –  read current control values into st->draft
// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::LoadDraft(State* st) {
    // General tab
    HWND pg = st->pages[TAB_GENERAL];
    if (pg) {
        int lang = static_cast<int>(
            SendMessageW(GetDlgItem(pg, ID_GEN_LANG), CB_GETCURSEL, 0, 0));
        st->draft.language          = (lang == 1) ? "en" : "de";
        st->draft.start_with_windows=
            SendMessageW(GetDlgItem(pg, ID_GEN_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED;
        st->draft.start_minimized   =
            SendMessageW(GetDlgItem(pg, ID_GEN_MINIMIZED),  BM_GETCHECK, 0, 0) == BST_CHECKED;
        st->draft.notifications     =
            SendMessageW(GetDlgItem(pg, ID_GEN_NOTIFY),     BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    // Audio tab
    pg = st->pages[TAB_AUDIO];
    if (pg) {
        st->draft.mic_volume  =
            static_cast<float>(SendMessageW(GetDlgItem(pg, ID_AUD_MIC_VOL), TBM_GETPOS, 0, 0))
            / 100.0f;
        st->draft.out_volume  =
            static_cast<float>(SendMessageW(GetDlgItem(pg, ID_AUD_OUT_VOL), TBM_GETPOS, 0, 0))
            / 100.0f;
        st->draft.vad_threshold =
            static_cast<float>(SendMessageW(GetDlgItem(pg, ID_AUD_VAD_THRESH), TBM_GETPOS, 0, 0))
            / 100.0f;

        int rate_sel = static_cast<int>(
            SendMessageW(GetDlgItem(pg, ID_AUD_BITRATE), CB_GETCURSEL, 0, 0));
        int rate_vals[] = { 32000, 48000, 64000, 96000, 128000 };
        if (rate_sel >= 0 && rate_sel < 5)
            st->draft.bitrate = rate_vals[rate_sel];

        bool ptt = SendMessageW(GetDlgItem(pg, ID_AUD_PTT), BM_GETCHECK, 0, 0) == BST_CHECKED;
        st->draft.voice_mode   = ptt ? VoiceMode::PushToTalk : VoiceMode::VoiceActivation;
        st->draft.echo_cancel  =
            SendMessageW(GetDlgItem(pg, ID_AUD_ECHO),  BM_GETCHECK, 0, 0) == BST_CHECKED;
        st->draft.noise_suppress =
            SendMessageW(GetDlgItem(pg, ID_AUD_NOISE), BM_GETCHECK, 0, 0) == BST_CHECKED;
        st->draft.auto_gain    =
            SendMessageW(GetDlgItem(pg, ID_AUD_AGC),   BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    // Appearance tab
    pg = st->pages[TAB_APPEARANCE];
    if (pg) {
        if (SendMessageW(GetDlgItem(pg, ID_APP_DARK),  BM_GETCHECK, 0, 0) == BST_CHECKED)
            st->draft.theme = ThemeMode::Dark;
        else if (SendMessageW(GetDlgItem(pg, ID_APP_LIGHT), BM_GETCHECK, 0, 0) == BST_CHECKED)
            st->draft.theme = ThemeMode::Light;
        else
            st->draft.theme = ThemeMode::Auto;
    }

    // Network tab
    pg = st->pages[TAB_NETWORK];
    if (pg) {
        st->draft.host     = W2U(GetDlgItemTextW_(pg, ID_NET_HOST));
        st->draft.tcp_port = _wtoi(GetDlgItemTextW_(pg, ID_NET_TCP_PORT).c_str());
        st->draft.udp_port = _wtoi(GetDlgItemTextW_(pg, ID_NET_UDP_PORT).c_str());
        st->draft.verify_tls =
            SendMessageW(GetDlgItem(pg, ID_NET_VERIFY_TLS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::ApplyDraft(State* st) {
    LoadDraft(st);
    *st->cfg = st->draft;
    Config_Save(*st->cfg);

    // Apply live audio settings
    if (g_voice) {
        g_voice->SetMicVolume  (st->cfg->mic_volume);
        g_voice->SetOutVolume  (st->cfg->out_volume);
        g_voice->SetBitrate    (st->cfg->bitrate);
        g_voice->SetVoiceMode  (st->cfg->voice_mode, st->cfg->ptt_key);
        g_voice->SetVadThreshold(st->cfg->vad_threshold);
    }

    // Apply theme
    Theme::Refresh(st->cfg->theme);

    st->saved = true;
}

// ─────────────────────────────────────────────────────────────────────────────
void SettingsWindow::ShowTab(State* st, int idx) {
    for (int i = 0; i < 4; ++i) {
        if (st->pages[i])
            ShowWindow(st->pages[i], (i == idx) ? SW_SHOW : SW_HIDE);
    }
    st->cur_tab = idx;
}

void SettingsWindow::ThemeControls(HWND hwnd) {
    InvalidateRect(hwnd, nullptr, TRUE);
    EnumChildWindows(hwnd, [](HWND c, LPARAM) -> BOOL {
        InvalidateRect(c, nullptr, TRUE);
        return TRUE;
    }, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dialog procedure
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK SettingsWindow::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = GetState(hwnd);

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        st->draft = *st->cfg;
        st->font  = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        // Tab control
        INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_TAB_CLASSES };
        InitCommonControlsEx(&icex);

        st->tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            6, 6, 500, 360, hwnd,
            reinterpret_cast<HMENU>(9999),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(st->tab, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

        const wchar_t* tab_labels[] = {
            L"Allgemein", L"Audio", L"Darstellung", L"Netzwerk"
        };
        for (int i = 0; i < 4; ++i) {
            TCITEMW tc{};
            tc.mask    = TCIF_TEXT;
            tc.pszText = const_cast<wchar_t*>(tab_labels[i]);
            TabCtrl_InsertItem(st->tab, i, &tc);
        }

        // Build pages (initially hidden; ShowTab shows the right one)
        st->pages[TAB_GENERAL]    = BuildPageGeneral   (hwnd, st);
        st->pages[TAB_AUDIO]      = BuildPageAudio     (hwnd, st);
        st->pages[TAB_APPEARANCE] = BuildPageAppearance(hwnd, st);
        st->pages[TAB_NETWORK]    = BuildPageNetwork   (hwnd, st);

        // Position pages inside the tab control display area
        RECT tab_rc = { 6, 6, 506, 366 };
        TabCtrl_AdjustRect(st->tab, FALSE, &tab_rc);
        for (int i = 0; i < 4; ++i) {
            if (st->pages[i])
                SetWindowPos(st->pages[i], HWND_TOP,
                             tab_rc.left, tab_rc.top,
                             tab_rc.right  - tab_rc.left,
                             tab_rc.bottom - tab_rc.top,
                             SWP_NOACTIVATE);
        }

        // Dialog buttons
        auto MkBtn = [&](const wchar_t* t, int id, int x) {
            HWND hc = CreateWindowExW(0, L"BUTTON", t,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, 376, 100, 28, hwnd,
                reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hc, WM_SETFONT,
                         reinterpret_cast<WPARAM>(st->font), TRUE);
        };
        MkBtn(L"OK",         ID_OK,     290);
        MkBtn(L"Abbrechen",  ID_CANCEL, 198);
        MkBtn(L"Übernehmen", ID_APPLY,  104);

        ShowTab(st, 0);
        return 0;
    }

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (st && hdr->hwndFrom == st->tab && hdr->code == TCN_SELCHANGE)
            ShowTab(st, TabCtrl_GetCurSel(st->tab));
        break;
    }

    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wp)) {
        case ID_OK:
            ApplyDraft(st);
            DestroyWindow(hwnd);
            break;
        case ID_CANCEL:
            DestroyWindow(hwnd);
            break;
        case ID_APPLY:
            ApplyDraft(st);
            break;
        case ID_AUD_MIC_TEST:
            if (g_voice) {
                static bool testing = false;
                testing = !testing;
                if (testing) {
                    g_voice->StartMicTest();
                    SetWindowTextW(GetDlgItem(st->pages[TAB_AUDIO], ID_AUD_MIC_TEST),
                                   L"Mikrofontest stoppen");
                } else {
                    g_voice->StopMicTest();
                    SetWindowTextW(GetDlgItem(st->pages[TAB_AUDIO], ID_AUD_MIC_TEST),
                                   L"Mikrofontest starten");
                }
            }
            break;
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        const Palette& c = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, c.fg_primary);
        SetBkMode(hdc, TRANSPARENT);
        if (msg == WM_CTLCOLOREDIT) {
            SetBkColor(hdc, c.bg_input);
            return reinterpret_cast<LRESULT>(Theme::Cache().br_input);
        }
        SetBkColor(hdc, c.bg_main);
        return reinterpret_cast<LRESULT>(Theme::Cache().br_main);
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, Theme::Cache().br_main);
        return 1;
    }

    case WM_SETTINGCHANGE:
        if (st && st->cfg->theme == ThemeMode::Auto) {
            Theme::Refresh();
            Theme::ApplyTitleBar(hwnd);
            ThemeControls(hwnd);
        }
        break;

    case WM_DESTROY:
        if (st) {
            if (st->font) DeleteObject(st->font);
            delete st;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────
bool SettingsWindow::Open(HINSTANCE hInst, HWND parent, AppConfig& cfg) {
    // Register window and page classes (idempotent)
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DlgProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = DLG_CLASS;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.cbWndExtra    = sizeof(LONG_PTR);
        RegisterClassExW(&wc);

        wc.lpfnWndProc   = DefWindowProcW;
        wc.lpszClassName = PAGE_CLASS;
        wc.hbrBackground = nullptr;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* st = new State();
    st->cfg = &cfg;

    int w = 520, h = 420;
    RECT pr; GetWindowRect(parent, &pr);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        DLG_CLASS, L"Einstellungen",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        pr.left + (pr.right - pr.left - w) / 2,
        pr.top  + (pr.bottom - pr.top - h) / 2,
        w, h, parent, nullptr, hInst, st);

    Theme::ApplyTitleBar(hwnd);
    ShowWindow(hwnd, SW_SHOW);

    // Modal message loop
    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return st ? st->saved : false;
}
