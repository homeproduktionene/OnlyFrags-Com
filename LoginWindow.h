// ─────────────────────────────────────────────────────────────────────────────
// LoginWindow.cpp  –  OnlyFrags Com
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#pragma comment(lib, "comctl32.lib")

#include <string>
#include <codecvt>
#include <locale>

#include "LoginWindow.h"
#include "Theme.h"
#include "Protocol.h"

// Link against the network client (extern defined in main.cpp)
#include "Network.h"
extern NetworkClient* g_net;

// ── UTF helpers ───────────────────────────────────────────────────────────────
static std::wstring GetWndText(HWND hwnd, int ctrl_id) {
    HWND hc = GetDlgItem(hwnd, ctrl_id);
    int  len = GetWindowTextLengthW(hc) + 1;
    std::wstring buf(len, L'\0');
    GetWindowTextW(hc, buf.data(), len);
    buf.resize(wcslen(buf.c_str()));
    return buf;
}
static std::string WstrToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

// ── Window class name ─────────────────────────────────────────────────────────
static constexpr wchar_t LOGIN_CLASS[]  = L"OFC_LoginWindow";
static constexpr wchar_t REG_CLASS[]    = L"OFC_RegisterWindow";

// ─────────────────────────────────────────────────────────────────────────────
// Theme helpers
// ─────────────────────────────────────────────────────────────────────────────
void LoginWindow::ApplyThemeToControls(HWND hwnd) {
    const Palette& c = Theme::Colors();
    // Force a repaint so WM_CTLCOLOR* fires for all children
    InvalidateRect(hwnd, nullptr, TRUE);
    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
        InvalidateRect(child, nullptr, TRUE);
        return TRUE;
    }, 0);
}

void LoginWindow::SetStatus(HWND hwnd, const std::wstring& msg, bool is_error) {
    HWND hs = GetDlgItem(hwnd, IDC_STATUS);
    SetWindowTextW(hs, msg.c_str());
    // Colour is handled in WM_CTLCOLORSTATIC; store error flag via window prop
    SetPropW(hs, L"isError", reinterpret_cast<HANDLE>(is_error ? 1 : 0));
    InvalidateRect(hs, nullptr, TRUE);
}

void LoginWindow::SetRegStatus(HWND hreg, const std::wstring& msg, bool is_error) {
    HWND hs = GetDlgItem(hreg, IDR_STATUS);
    SetWindowTextW(hs, msg.c_str());
    SetPropW(hs, L"isError", reinterpret_cast<HANDLE>(is_error ? 1 : 0));
    InvalidateRect(hs, nullptr, TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Create the Login window
// ─────────────────────────────────────────────────────────────────────────────
HWND LoginWindow::Create(HINSTANCE hInst, AppConfig& cfg, LoginCallback on_login) {
    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = LOGIN_CLASS;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.cbWndExtra    = sizeof(LONG_PTR);
    RegisterClassExW(&wc);

    // Allocate state
    auto* st = new State();
    st->cfg      = &cfg;
    st->on_login = std::move(on_login);

    // Create window
    int w = 380, h = 320;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExW(
        0, LOGIN_CLASS, L"OnlyFrags Com",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (screen_w - w) / 2, (screen_h - h) / 2, w, h,
        nullptr, nullptr, hInst, st);

    Theme::ApplyTitleBar(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Login window procedure
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK LoginWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = GetState(hwnd);

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        const Palette& c = Theme::Colors();
        HFONT hfont  = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hbold  = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        auto MakeCtrl = [&](const wchar_t* cls, const wchar_t* txt,
                            DWORD style, int x, int y, int w, int h, int id) -> HWND {
            HWND hc = CreateWindowExW(0, cls, txt,
                WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
            return hc;
        };

        // Title
        HWND htitle = MakeCtrl(L"STATIC", L"OnlyFrags Com",
                               SS_CENTER, 10, 20, 340, 32, 0);
        SendMessageW(htitle, WM_SETFONT, reinterpret_cast<WPARAM>(hbold), TRUE);

        // Server label
        std::wstring host_label = L"Server: " +
            std::wstring(st->cfg->host.begin(), st->cfg->host.end()) +
            L":" + std::to_wstring(st->cfg->tcp_port);
        MakeCtrl(L"STATIC", host_label.c_str(), SS_CENTER, 10, 55, 340, 18, IDC_HOST_LABEL);

        // Email
        MakeCtrl(L"STATIC",  L"E-Mail",    0,             30,  85, 300, 18, 0);
        MakeCtrl(L"EDIT",    L"",          ES_AUTOHSCROLL, 30, 105, 300, 28, IDC_EMAIL);

        // Password
        MakeCtrl(L"STATIC",  L"Passwort",  0,             30, 143, 300, 18, 0);
        MakeCtrl(L"EDIT",    L"",
                 ES_AUTOHSCROLL | ES_PASSWORD, 30, 163, 300, 28, IDC_PASSWORD);

        // Status label
        MakeCtrl(L"STATIC",  L"",          SS_CENTER,     30, 198, 300, 20, IDC_STATUS);

        // Buttons
        MakeCtrl(L"BUTTON",  L"Anmelden",  BS_DEFPUSHBUTTON, 30, 225, 140, 34, IDC_BTN_LOGIN);
        MakeCtrl(L"BUTTON",  L"Registrieren", 0,           188, 225, 140, 34, IDC_BTN_REGISTER);

        // Set background
        SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(Theme::Cache().br_main));
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const Palette& c = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND hctl = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, TRANSPARENT);

        // Status label
        if (GetDlgCtrlID(hctl) == IDC_STATUS) {
            bool err = GetPropW(hctl, L"isError") != nullptr;
            SetTextColor(hdc, err ? RGB(220, 80, 80) : RGB(80, 200, 120));
            SetBkColor(hdc, c.bg_main);
            return reinterpret_cast<LRESULT>(Theme::Cache().br_main);
        }

        SetTextColor(hdc, c.fg_primary);
        SetBkColor(hdc, (msg == WM_CTLCOLOREDIT) ? c.bg_input : c.bg_main);
        return reinterpret_cast<LRESULT>(
            (msg == WM_CTLCOLOREDIT) ? Theme::Cache().br_input
                                     : Theme::Cache().br_main);
    }

    case WM_CTLCOLORBTN: {
        // Buttons handled by BS_OWNERDRAW or default
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wp)) {
        case IDC_BTN_LOGIN: {
            if (st->waiting) break;
            if (!g_net || !g_net->IsConnected()) {
                SetStatus(hwnd, L"Keine Verbindung zum Server.", true);
                break;
            }
            std::wstring email = GetWndText(hwnd, IDC_EMAIL);
            std::wstring pass  = GetWndText(hwnd, IDC_PASSWORD);
            if (email.empty() || pass.empty()) {
                SetStatus(hwnd, L"Bitte alle Felder ausfüllen.", true);
                break;
            }
            st->waiting = true;
            SetStatus(hwnd, L"Verbindung wird hergestellt …", false);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_LOGIN),    FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REGISTER), FALSE);
            g_net->SendLogin(WstrToUtf8(email), WstrToUtf8(pass));
            break;
        }
        case IDC_BTN_REGISTER:
            OpenRegisterDialog(hwnd, st);
            break;
        }
        break;

    // ── Messages from network thread ──────────────────────────────────────
    case WM_APP_LOGIN_OK: {
        auto* name = reinterpret_cast<std::wstring*>(lp);
        if (st && st->on_login)
            st->on_login(*name, g_net ? g_net->GetToken() : "");
        delete name;
        break;
    }
    case WM_APP_LOGIN_FAIL: {
        auto* msg_str = reinterpret_cast<std::wstring*>(lp);
        if (st) {
            st->waiting = false;
            SetStatus(hwnd, *msg_str, true);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_LOGIN),    TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_REGISTER), TRUE);
        }
        delete msg_str;
        break;
    }
    case WM_APP_REGISTER_OK: {
        if (st && st->hwnd_reg) {
            SetRegStatus(st->hwnd_reg,
                L"Registrierung erfolgreich! Du kannst dich jetzt anmelden.", false);
            EnableWindow(GetDlgItem(st->hwnd_reg, IDR_BTN_SUBMIT), TRUE);
        }
        break;
    }
    case WM_APP_REGISTER_FAIL: {
        auto* msg_str = reinterpret_cast<std::wstring*>(lp);
        if (st && st->hwnd_reg) {
            SetRegStatus(st->hwnd_reg, *msg_str, true);
            EnableWindow(GetDlgItem(st->hwnd_reg, IDR_BTN_SUBMIT), TRUE);
        }
        delete msg_str;
        break;
    }
    case WM_APP_CONNECTED: {
        SetStatus(hwnd, L"Verbunden. Bitte anmelden.", false);
        break;
    }
    case WM_APP_DISCONNECTED: {
        if (st) st->waiting = false;
        SetStatus(hwnd, L"Keine Verbindung zum Server.", true);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_LOGIN),    TRUE);
        EnableWindow(GetDlgItem(hwnd, IDC_BTN_REGISTER), TRUE);
        break;
    }

    case WM_SETTINGCHANGE:
        Theme::Refresh();
        Theme::ApplyTitleBar(hwnd);
        ApplyThemeToControls(hwnd);
        break;

    case WM_KEYDOWN:
        if (wp == VK_RETURN)
            SendMessageW(hwnd, WM_COMMAND, IDC_BTN_LOGIN, 0);
        break;

    case WM_DESTROY:
        delete st;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Register dialog
// ─────────────────────────────────────────────────────────────────────────────
void LoginWindow::OpenRegisterDialog(HWND parent, State* st) {
    if (st->hwnd_reg && IsWindow(st->hwnd_reg)) {
        SetForegroundWindow(st->hwnd_reg);
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = RegisterDlgProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = REG_CLASS;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.cbWndExtra    = sizeof(LONG_PTR);
    RegisterClassExW(&wc);

    int w = 380, h = 380;
    RECT pr; GetWindowRect(parent, &pr);

    HWND hreg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        REG_CLASS, L"Registrieren",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        pr.left + 20, pr.top + 20, w, h,
        parent, nullptr, GetModuleHandleW(nullptr), st);

    st->hwnd_reg = hreg;
    Theme::ApplyTitleBar(hreg);
    ShowWindow(hreg, SW_SHOW);
}

LRESULT CALLBACK LoginWindow::RegisterDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = GetState(hwnd);

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        HFONT hfont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        auto Mk = [&](const wchar_t* cls, const wchar_t* txt,
                      DWORD style, int x, int y, int w, int h, int id) -> HWND {
            HWND hc = CreateWindowExW(0, cls, txt,
                WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(hfont), TRUE);
            return hc;
        };

        Mk(L"STATIC", L"Benutzername", 0,             30,  18, 300, 18, 0);
        Mk(L"EDIT",   L"",  ES_AUTOHSCROLL,            30,  38, 300, 28, IDR_USERNAME);

        Mk(L"STATIC", L"E-Mail",       0,             30,  76, 300, 18, 0);
        Mk(L"EDIT",   L"",  ES_AUTOHSCROLL,            30,  96, 300, 28, IDR_EMAIL);

        Mk(L"STATIC", L"Passwort",     0,             30, 134, 300, 18, 0);
        Mk(L"EDIT",   L"",  ES_AUTOHSCROLL|ES_PASSWORD, 30, 154, 300, 28, IDR_PASSWORD);

        Mk(L"STATIC", L"Invite-Code",  0,             30, 192, 300, 18, 0);
        Mk(L"EDIT",   L"",  ES_AUTOHSCROLL,            30, 212, 300, 28, IDR_INVITE);

        Mk(L"STATIC", L"",             SS_CENTER,     30, 248, 300, 20, IDR_STATUS);

        Mk(L"BUTTON", L"Registrieren", BS_DEFPUSHBUTTON, 30, 278, 140, 34, IDR_BTN_SUBMIT);
        Mk(L"BUTTON", L"Abbrechen",    0,             188, 278, 140, 34, IDR_BTN_CANCEL);

        SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(Theme::Cache().br_main));
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const Palette& c = Theme::Colors();
        HDC  hdc  = reinterpret_cast<HDC>(wp);
        HWND hctl = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, TRANSPARENT);
        if (GetDlgCtrlID(hctl) == IDR_STATUS) {
            bool err = GetPropW(hctl, L"isError") != nullptr;
            SetTextColor(hdc, err ? RGB(220, 80, 80) : RGB(80, 200, 120));
            SetBkColor(hdc, c.bg_main);
            return reinterpret_cast<LRESULT>(Theme::Cache().br_main);
        }
        SetTextColor(hdc, c.fg_primary);
        SetBkColor(hdc, (msg == WM_CTLCOLOREDIT) ? c.bg_input : c.bg_main);
        return reinterpret_cast<LRESULT>(
            (msg == WM_CTLCOLOREDIT) ? Theme::Cache().br_input
                                     : Theme::Cache().br_main);
    }

    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wp)) {
        case IDR_BTN_SUBMIT: {
            if (!g_net || !g_net->IsConnected()) {
                LoginWindow::SetRegStatus(hwnd, L"Keine Verbindung.", true);
                break;
            }
            std::wstring uname = GetWndText(hwnd, IDR_USERNAME);
            std::wstring email = GetWndText(hwnd, IDR_EMAIL);
            std::wstring pass  = GetWndText(hwnd, IDR_PASSWORD);
            std::wstring inv   = GetWndText(hwnd, IDR_INVITE);
            if (uname.empty() || email.empty() || pass.empty() || inv.empty()) {
                LoginWindow::SetRegStatus(hwnd, L"Alle Felder ausfüllen.", true);
                break;
            }
            EnableWindow(GetDlgItem(hwnd, IDR_BTN_SUBMIT), FALSE);
            LoginWindow::SetRegStatus(hwnd, L"Registrierung wird durchgeführt …", false);
            g_net->SendRegister(
                WstrToUtf8(uname), WstrToUtf8(email),
                WstrToUtf8(pass),  WstrToUtf8(inv));
            break;
        }
        case IDR_BTN_CANCEL:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        if (st) st->hwnd_reg = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
