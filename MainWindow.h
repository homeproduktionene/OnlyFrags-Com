// ─────────────────────────────────────────────────────────────────────────────
// MainWindow.cpp  –  OnlyFrags Com
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#pragma comment(lib, "comctl32.lib")

#include "MainWindow.h"
#include "Theme.h"
#include "Network.h"
#include "VoiceEngine.h"
#include "SettingsWindow.h"
#include "Protocol.h"

extern NetworkClient* g_net;
extern VoiceEngine*   g_voice;

static constexpr wchar_t MAIN_CLASS[] = L"OFC_MainWindow";

// ── UTF helpers ───────────────────────────────────────────────────────────────
static std::string WstrToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), sz);
    return w;
}
static std::wstring GetWndText(HWND ctrl) {
    int len = GetWindowTextLengthW(ctrl) + 1;
    std::wstring buf(len, L'\0');
    GetWindowTextW(ctrl, buf.data(), len);
    buf.resize(wcslen(buf.c_str()));
    return buf;
}
// Format ISO timestamp "2024-01-01T12:34:56Z" → "12:34"
static std::wstring FormatTime(const std::wstring& iso) {
    if (iso.size() >= 16)
        return iso.substr(11, 5);
    return L"--:--";
}

// ─────────────────────────────────────────────────────────────────────────────
HWND MainWindow::Create(HINSTANCE hInst, AppConfig& cfg,
                        const std::wstring& username,
                        const std::string&  token) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = MAIN_CLASS;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.cbWndExtra    = sizeof(LONG_PTR);
    RegisterClassExW(&wc);

    auto* st       = new State();
    st->cfg        = &cfg;
    st->username   = username;
    st->token      = token;

    HWND hwnd = CreateWindowExW(
        0, MAIN_CLASS, L"OnlyFrags Com",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 640,
        nullptr, nullptr, hInst, st);

    Theme::ApplyTitleBar(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Request history for default channel
    if (g_net && g_net->IsConnected())
        g_net->GetHistory(WstrToUtf8(st->active_text_channel));

    return hwnd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout – called on WM_CREATE and WM_SIZE
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::LayoutChildren(HWND hwnd, State* st) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Sidebar: full height
    int sb_w = SIDEBAR_W;

    // Text channel list: top 1/3 of sidebar
    int text_h    = (H - TOPBAR_H - BOTTOMBAR_H) / 3;
    int voice_h   = text_h;
    int user_h    = H - TOPBAR_H - BOTTOMBAR_H - text_h - voice_h;

    // Chat area
    int chat_x    = sb_w;
    int chat_y    = TOPBAR_H;
    int chat_w    = W - sb_w;
    int chat_h    = H - TOPBAR_H - BOTTOMBAR_H - INPUT_H;
    int input_y   = H - BOTTOMBAR_H - INPUT_H;

    // Text channel list
    SetWindowPos(st->hwnd_chan_list,  nullptr,
        0, TOPBAR_H, sb_w, text_h, SWP_NOZORDER | SWP_NOACTIVATE);
    // Voice channel list
    SetWindowPos(st->hwnd_voice_list, nullptr,
        0, TOPBAR_H + text_h, sb_w, voice_h, SWP_NOZORDER | SWP_NOACTIVATE);
    // Voice user list
    SetWindowPos(st->hwnd_user_list,  nullptr,
        0, TOPBAR_H + text_h + voice_h, sb_w, user_h, SWP_NOZORDER | SWP_NOACTIVATE);

    // Chat display
    SetWindowPos(st->hwnd_chat_list,  nullptr,
        chat_x, chat_y, chat_w, chat_h, SWP_NOZORDER | SWP_NOACTIVATE);

    // Chat input
    SetWindowPos(st->hwnd_input,      nullptr,
        chat_x + 8, input_y + 6, chat_w - 70, 28, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hwnd, IDC_BTN_SEND), nullptr,
        chat_x + chat_w - 58, input_y + 6, 50, 28, SWP_NOZORDER | SWP_NOACTIVATE);

    // Bottom bar buttons
    int bbar_y  = H - BOTTOMBAR_H;
    int btn_w   = 44;
    SetWindowPos(st->hwnd_mute,     nullptr, 6, bbar_y + 6, btn_w, btn_w, SWP_NOZORDER);
    SetWindowPos(st->hwnd_deafen,   nullptr, 58, bbar_y + 6, btn_w, btn_w, SWP_NOZORDER);
    SetWindowPos(st->hwnd_settings, nullptr, W - 50, bbar_y + 6, btn_w, btn_w, SWP_NOZORDER);
    SetWindowPos(st->hwnd_status_bar, nullptr,
        110, bbar_y + 14, W - 170, 22, SWP_NOZORDER);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::PopulateChannels(HWND hwnd, State* st) {
    // Text channels
    SendMessageW(st->hwnd_chan_list, LB_RESETCONTENT, 0, 0);
    for (const auto& ch : st->text_channels) {
        std::wstring label = L"# " + ch;
        SendMessageW(st->hwnd_chan_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
    // Select active
    for (int i = 0; i < static_cast<int>(st->text_channels.size()); ++i) {
        if (st->text_channels[i] == st->active_text_channel) {
            SendMessageW(st->hwnd_chan_list, LB_SETCURSEL, i, 0);
            break;
        }
    }

    // Voice channels
    SendMessageW(st->hwnd_voice_list, LB_RESETCONTENT, 0, 0);
    for (const auto& ch : st->voice_channels) {
        int count = 0;
        auto it = st->voice_counts.find(ch);
        if (it != st->voice_counts.end()) count = it->second;
        std::wstring label = L"🔊 " + ch;
        if (count > 0) label += L" (" + std::to_wstring(count) + L")";
        SendMessageW(st->hwnd_voice_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::RefreshChatDisplay(State* st) {
    SendMessageW(st->hwnd_chat_list, LB_RESETCONTENT, 0, 0);
    auto it = st->chat_log.find(st->active_text_channel);
    if (it == st->chat_log.end()) return;
    for (const auto& line : it->second) {
        auto* copy = new ChatLine(line);
        SendMessageW(st->hwnd_chat_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(copy));
    }
    // Scroll to bottom
    int count = static_cast<int>(SendMessageW(st->hwnd_chat_list, LB_GETCOUNT, 0, 0));
    if (count > 0)
        SendMessageW(st->hwnd_chat_list, LB_SETTOPINDEX, count - 1, 0);
}

void MainWindow::AppendChatLine(State* st, const ChatLine& line) {
    auto& log = st->chat_log[st->active_text_channel];
    log.push_back(line);
    if (static_cast<int>(log.size()) > State::CHAT_MAX_LINES)
        log.pop_front();

    auto* copy = new ChatLine(line);
    SendMessageW(st->hwnd_chat_list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(copy));
    int count = static_cast<int>(SendMessageW(st->hwnd_chat_list, LB_GETCOUNT, 0, 0));
    if (count > 0)
        SendMessageW(st->hwnd_chat_list, LB_SETTOPINDEX, count - 1, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::UpdateVoiceUsers(HWND hwnd, State* st,
                                   const std::wstring& channel,
                                   const std::vector<VoiceUser>& users) {
    st->voice_users[channel] = users;
    if (channel != st->active_voice_channel) return;

    SendMessageW(st->hwnd_user_list, LB_RESETCONTENT, 0, 0);
    for (const auto& u : users) {
        std::wstring label = u.speaking ? L"▶ " : L"   ";
        label += u.username;
        if (u.muted)    label += L" 🔇";
        if (u.deafened) label += L" 🔕";
        SendMessageW(st->hwnd_user_list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::SendChatMessage(HWND hwnd, State* st) {
    std::wstring text = GetWndText(st->hwnd_input);
    // Trim whitespace
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n'))
        text.pop_back();
    if (text.empty()) return;

    if (g_net && g_net->IsConnected())
        g_net->SendChat(WstrToUtf8(st->active_text_channel), WstrToUtf8(text));

    SetWindowTextW(st->hwnd_input, L"");
}

// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::UpdateMuteButtons(State* st) {
    const Palette& c = Theme::Colors();
    // Button text doubles as state indicator
    SetWindowTextW(st->hwnd_mute,   st->muted    ? L"🔇" : L"🎤");
    SetWindowTextW(st->hwnd_deafen, st->deafened ? L"🔕" : L"🎧");
}

// ─────────────────────────────────────────────────────────────────────────────
// Owner-draw chat list items
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::MeasureChatItem(HWND hwnd, State* st, MEASUREITEMSTRUCT* mis) {
    mis->itemHeight = 44;  // two lines: username + message
}

void MainWindow::DrawChatItem(HWND hwnd, State* st, DRAWITEMSTRUCT* dis) {
    if (dis->itemID == LB_ERR) return;
    const auto* line = reinterpret_cast<const ChatLine*>(dis->itemData);
    if (!line) return;

    const Palette& c = Theme::Colors();
    HDC hdc   = dis->hDC;
    RECT r    = dis->rcItem;

    // Background
    bool sel = (dis->itemState & ODS_SELECTED) != 0;
    FillRect(hdc, &r, sel ? Theme::Cache().br_selected : Theme::Cache().br_secondary);

    // Username line
    SelectObject(hdc, st->font_bold);
    SetTextColor(hdc, c.accent);
    SetBkMode(hdc, TRANSPARENT);

    RECT user_r = { r.left + 8, r.top + 4, r.right - 60, r.top + 22 };
    DrawTextW(hdc, line->username.c_str(), -1, &user_r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Timestamp
    SelectObject(hdc, st->font_small);
    SetTextColor(hdc, c.fg_secondary);
    RECT ts_r = { r.right - 58, r.top + 6, r.right - 4, r.top + 22 };
    DrawTextW(hdc, line->timestamp.c_str(), -1, &ts_r, DT_RIGHT | DT_SINGLELINE);

    // Message content
    SelectObject(hdc, st->font_normal);
    SetTextColor(hdc, c.fg_primary);
    RECT msg_r = { r.left + 8, r.top + 22, r.right - 4, r.bottom - 2 };
    DrawTextW(hdc, line->content.c_str(), -1, &msg_r,
              DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Separator line
    HPEN old = static_cast<HPEN>(SelectObject(hdc, Theme::Cache().pen_border));
    MoveToEx(hdc, r.left, r.bottom - 1, nullptr);
    LineTo(hdc, r.right, r.bottom - 1);
    SelectObject(hdc, old);
}

// ─────────────────────────────────────────────────────────────────────────────
// WndProc
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = GetState(hwnd);

    switch (msg) {
    // ── Creation ─────────────────────────────────────────────────────────────
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

        // Default channels until server sends them
        st->text_channels  = { L"general", L"offtopic" };
        st->voice_channels = { L"Live",
            L"Live Game 1",  L"Live Game 2",  L"Live Game 3",
            L"Live Game 4",  L"Live Game 5",  L"Live Game 6",
            L"Live Game 7",  L"Live Game 8",  L"Live Game 9",
            L"Live Game 10" };

        HINSTANCE hInst = cs->hInstance;
        st->font_normal = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        st->font_small  = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        st->font_bold   = CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        auto Mk = [&](const wchar_t* cls, DWORD style, int x, int y,
                      int w, int h, int id, HFONT font = nullptr) -> HWND {
            HWND hc = CreateWindowExW(0, cls, L"",
                WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                hInst, nullptr);
            SendMessageW(hc, WM_SETFONT,
                         reinterpret_cast<WPARAM>(font ? font : st->font_normal), TRUE);
            return hc;
        };

        st->hwnd_chan_list  = Mk(L"LISTBOX",
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
            0, 0, 0, 0, IDC_CHAN_LIST);
        st->hwnd_voice_list = Mk(L"LISTBOX",
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
            0, 0, 0, 0, IDC_VOICE_LIST);
        st->hwnd_user_list  = Mk(L"LISTBOX",
            LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
            0, 0, 0, 0, IDC_USER_LIST);

        // Chat display: owner-draw variable
        st->hwnd_chat_list  = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
            0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(IDC_CHAT_LIST), hInst, nullptr);

        // Chat input (multiline, intercept Enter)
        st->hwnd_input = Mk(L"EDIT",
            ES_AUTOHSCROLL | WS_BORDER,
            0, 0, 0, 0, IDC_CHAT_INPUT);

        // Send button
        Mk(L"BUTTON", 0, 0, 0, 0, 0, IDC_BTN_SEND);
        SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_SEND), L"↩");

        // Bottom bar controls
        st->hwnd_mute     = Mk(L"BUTTON", 0, 0, 0, 0, 0, IDC_BTN_MUTE);
        st->hwnd_deafen   = Mk(L"BUTTON", 0, 0, 0, 0, 0, IDC_BTN_DEAFEN);
        st->hwnd_settings = Mk(L"BUTTON", 0, 0, 0, 0, 0, IDC_BTN_SETTINGS);
        SetWindowTextW(st->hwnd_settings, L"⚙");

        st->hwnd_status_bar = Mk(L"STATIC", SS_LEFT, 0, 0, 0, 0, IDC_STATUS_BAR,
                                 st->font_small);
        std::wstring status = L"Angemeldet als: " + st->username;
        SetWindowTextW(st->hwnd_status_bar, status.c_str());

        UpdateMuteButtons(st);
        PopulateChannels(hwnd, st);

        // Subclass chat input to intercept VK_RETURN
        SetPropW(st->hwnd_input, L"OrigProc",
                 reinterpret_cast<HANDLE>(
                     SetWindowLongPtrW(st->hwnd_input, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>([](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                             if (m == WM_KEYDOWN && w == VK_RETURN &&
                                 !(GetKeyState(VK_SHIFT) & 0x8000)) {
                                 SendMessageW(GetParent(h), WM_COMMAND,
                                              MAKEWPARAM(IDC_BTN_SEND, BN_CLICKED), 0);
                                 return 0;
                             }
                             auto orig = reinterpret_cast<WNDPROC>(GetPropW(h, L"OrigProc"));
                             return CallWindowProcW(orig, h, m, w, l);
                         })
                     )
                 ));

        return 0;
    }

    case WM_SIZE:
        if (st) LayoutChildren(hwnd, st);
        break;

    // ── Painting ──────────────────────────────────────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        const Palette& c = Theme::Colors();

        // Top bar
        RECT top = { 0, 0, rc.right, TOPBAR_H };
        FillRect(hdc, &top, Theme::Cache().br_main);

        // Server name
        SelectObject(hdc, st ? st->font_bold : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, c.fg_primary);
        SetBkMode(hdc, TRANSPARENT);
        RECT title_r = { SIDEBAR_W + 12, 0, rc.right, TOPBAR_H };
        DrawTextW(hdc, st ? (L"# " + st->active_text_channel).c_str()
                           : L"OnlyFrags Com",
                  -1, &title_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Sidebar top header
        RECT sidebar_top = { 0, 0, SIDEBAR_W, TOPBAR_H };
        FillRect(hdc, &sidebar_top, Theme::Cache().br_main);
        RECT srv_r = { 4, 0, SIDEBAR_W - 4, TOPBAR_H };
        DrawTextW(hdc, L"OnlyFrags Com", -1, &srv_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Sidebar section labels
        int tH  = (rc.bottom - TOPBAR_H - BOTTOMBAR_H) / 3;
        int vH  = tH;
        SelectObject(hdc, st ? st->font_small : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, c.fg_secondary);
        RECT tl_r = { 4, TOPBAR_H - 2, SIDEBAR_W, TOPBAR_H + 16 };
        DrawTextW(hdc, L"TEXTKANÄLE", -1, &tl_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT vl_r = { 4, TOPBAR_H + tH - 2, SIDEBAR_W, TOPBAR_H + tH + 16 };
        DrawTextW(hdc, L"SPRACHKANÄLE", -1, &vl_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT ul_r = { 4, TOPBAR_H + tH + vH - 2, SIDEBAR_W, TOPBAR_H + tH + vH + 16 };
        DrawTextW(hdc, L"NUTZER IM KANAL", -1, &ul_r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Vertical separator
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, Theme::Cache().pen_border));
        MoveToEx(hdc, SIDEBAR_W, TOPBAR_H, nullptr);
        LineTo(hdc, SIDEBAR_W, rc.bottom);
        // Horizontal separator (top bar bottom edge)
        MoveToEx(hdc, 0, TOPBAR_H, nullptr);
        LineTo(hdc, rc.right, TOPBAR_H);
        SelectObject(hdc, oldPen);

        // Bottom bar
        int bbar_y = rc.bottom - BOTTOMBAR_H;
        RECT bottom_r = { 0, bbar_y, rc.right, rc.bottom };
        FillRect(hdc, &bottom_r, Theme::Cache().br_main);
        MoveToEx(hdc, 0, bbar_y, nullptr);
        oldPen = static_cast<HPEN>(SelectObject(hdc, Theme::Cache().pen_border));
        LineTo(hdc, rc.right, bbar_y);
        SelectObject(hdc, oldPen);

        // Input area background
        int input_y = bbar_y - INPUT_H - 2;
        RECT inp_r = { SIDEBAR_W, input_y, rc.right, bbar_y };
        FillRect(hdc, &inp_r, Theme::Cache().br_secondary);

        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Control colours ───────────────────────────────────────────────────────
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const Palette& c = Theme::Colors();
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, c.fg_primary);
        SetBkMode(hdc, TRANSPARENT);
        if (msg == WM_CTLCOLOREDIT) {
            SetBkColor(hdc, c.bg_input);
            return reinterpret_cast<LRESULT>(Theme::Cache().br_input);
        }
        SetBkColor(hdc, c.bg_secondary);
        return reinterpret_cast<LRESULT>(Theme::Cache().br_secondary);
    }

    // ── Owner-draw (chat list) ────────────────────────────────────────────────
    case WM_MEASUREITEM:
        if (reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlID == IDC_CHAT_LIST)
            MeasureChatItem(hwnd, st, reinterpret_cast<MEASUREITEMSTRUCT*>(lp));
        break;
    case WM_DRAWITEM:
        if (reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlID == IDC_CHAT_LIST)
            DrawChatItem(hwnd, st, reinterpret_cast<DRAWITEMSTRUCT*>(lp));
        break;

    // ── Commands ──────────────────────────────────────────────────────────────
    case WM_COMMAND:
        if (!st) break;
        switch (LOWORD(wp)) {
        case IDC_BTN_SEND:
            SendChatMessage(hwnd, st);
            break;

        case IDC_BTN_MUTE:
            st->muted = !st->muted;
            if (g_voice) g_voice->SetMuted(st->muted);
            if (g_net)   g_net->SetMuted(st->muted);
            UpdateMuteButtons(st);
            break;

        case IDC_BTN_DEAFEN:
            st->deafened = !st->deafened;
            if (g_voice) g_voice->SetDeafened(st->deafened);
            if (g_net)   g_net->SetDeafened(st->deafened);
            UpdateMuteButtons(st);
            break;

        case IDC_BTN_SETTINGS:
            SettingsWindow::Open(GetModuleHandleW(nullptr), hwnd, *st->cfg);
            break;

        case IDC_CHAN_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int idx = static_cast<int>(
                    SendMessageW(st->hwnd_chan_list, LB_GETCURSEL, 0, 0));
                if (idx != LB_ERR && idx < static_cast<int>(st->text_channels.size())) {
                    st->active_text_channel = st->text_channels[idx];
                    RefreshChatDisplay(st);
                    // Request history if not cached
                    auto it = st->chat_log.find(st->active_text_channel);
                    if (it == st->chat_log.end() && g_net && g_net->IsConnected())
                        g_net->GetHistory(WstrToUtf8(st->active_text_channel));
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;

        case IDC_VOICE_LIST:
            if (HIWORD(wp) == LBN_DBLCLK) {
                int idx = static_cast<int>(
                    SendMessageW(st->hwnd_voice_list, LB_GETCURSEL, 0, 0));
                if (idx != LB_ERR && idx < static_cast<int>(st->voice_channels.size())) {
                    std::wstring ch = st->voice_channels[idx];
                    if (ch == st->active_voice_channel) {
                        // Already in channel – leave
                        st->active_voice_channel.clear();
                        if (g_net) g_net->LeaveVoiceChannel();
                        if (g_voice) g_voice->LeaveChannel();
                        SendMessageW(st->hwnd_user_list, LB_RESETCONTENT, 0, 0);
                    } else {
                        st->active_voice_channel = ch;
                        if (g_net) g_net->JoinVoiceChannel(WstrToUtf8(ch));
                        if (g_voice) {
                            g_voice->JoinChannel(WstrToUtf8(ch),
                                                  static_cast<uint8_t>(idx));
                        }
                    }
                }
            }
            break;
        }
        break;

    // ── Network messages ──────────────────────────────────────────────────────
    case WM_APP_CHAT_MSG: {
        auto* d = reinterpret_cast<ChatMessageData*>(lp);
        if (d && st) {
            // Store in correct channel
            auto& log = st->chat_log[d->channel];
            ChatLine line{ d->username, d->content, FormatTime(d->timestamp) };
            log.push_back(line);
            if (static_cast<int>(log.size()) > State::CHAT_MAX_LINES)
                log.pop_front();
            // Display if it's the active channel
            if (d->channel == st->active_text_channel)
                AppendChatLine(st, line);
        }
        delete d;
        break;
    }
    case WM_APP_VOICE_STATE: {
        auto* d = reinterpret_cast<VoiceStateData*>(lp);
        if (d && st) UpdateVoiceUsers(hwnd, st, d->channel, d->users);
        delete d;
        break;
    }
    case WM_APP_VOICE_COUNT: {
        auto* d = reinterpret_cast<VoiceCountData*>(lp);
        if (d && st) {
            st->voice_counts[d->channel] = d->count;
            PopulateChannels(hwnd, st);
        }
        delete d;
        break;
    }
    case WM_APP_NET_MSG: {
        // Full login_ok JSON with channel lists
        auto* j = reinterpret_cast<nlohmann::json*>(lp);
        if (j && st) {
            if (j->contains("text_channels") && (*j)["text_channels"].is_array()) {
                st->text_channels.clear();
                for (const auto& ch : (*j)["text_channels"])
                    st->text_channels.push_back(Utf8ToWide(ch.get<std::string>()));
            }
            if (j->contains("voice_channels") && (*j)["voice_channels"].is_array()) {
                st->voice_channels.clear();
                for (const auto& ch : (*j)["voice_channels"])
                    st->voice_channels.push_back(Utf8ToWide(ch.get<std::string>()));
            }
            PopulateChannels(hwnd, st);
        }
        delete j;
        break;
    }
    case WM_APP_JOINED_VOICE: {
        auto* ch = reinterpret_cast<std::wstring*>(lp);
        if (ch && st) {
            st->active_voice_channel = *ch;
            // Register UDP port with server
            if (g_voice && g_net) {
                int port = g_voice->BindUdp();
                if (port > 0) {
                    g_net->RegisterUdpPort(port);
                    g_voice->SetToken(st->token);
                }
            }
        }
        delete ch;
        break;
    }
    case WM_APP_UDP_READY: {
        if (st && g_voice && g_net) {
            g_voice->SetServerEndpoint(
                st->cfg->host,
                g_net->GetServerUdpPort());
        }
        break;
    }
    case WM_APP_DISCONNECTED:
        if (st) {
            st->active_voice_channel.clear();
            SendMessageW(st->hwnd_user_list, LB_RESETCONTENT, 0, 0);
        }
        break;

    // ── Theme ─────────────────────────────────────────────────────────────────
    case WM_SETTINGCHANGE:
        if (st && st->cfg->theme == ThemeMode::Auto) {
            Theme::Refresh(ThemeMode::Auto);
            Theme::ApplyTitleBar(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        break;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && st && !st->active_voice_channel.empty()) {
            st->active_voice_channel.clear();
            if (g_net)   g_net->LeaveVoiceChannel();
            if (g_voice) g_voice->LeaveChannel();
            SendMessageW(st->hwnd_user_list, LB_RESETCONTENT, 0, 0);
        }
        break;

    case WM_DELETEITEM: {
        // Free heap-allocated ChatLine items
        auto* dis = reinterpret_cast<DELETEITEMSTRUCT*>(lp);
        if (dis->CtlID == IDC_CHAT_LIST && dis->itemData)
            delete reinterpret_cast<ChatLine*>(dis->itemData);
        break;
    }

    case WM_DESTROY:
        if (st) {
            if (st->font_normal) DeleteObject(st->font_normal);
            if (st->font_small)  DeleteObject(st->font_small);
            if (st->font_bold)   DeleteObject(st->font_bold);
            delete st;
        }
        PostQuitMessage(0);
        break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
