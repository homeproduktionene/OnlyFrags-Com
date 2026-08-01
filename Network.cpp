#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MainWindow.h  –  OnlyFrags Com
// Main application window: sidebar (channels + voice users) + chat area.
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>

#include "Config.h"
#include "Protocol.h"

struct ChatLine {
    std::wstring username;
    std::wstring content;
    std::wstring timestamp;   // HH:MM
};

// ─────────────────────────────────────────────────────────────────────────────
class MainWindow {
public:
    static HWND Create(HINSTANCE hInst, AppConfig& cfg,
                       const std::wstring& username,
                       const std::string&  token);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    // ── Internal state held per-window ────────────────────────────────────────
    struct State {
        AppConfig*    cfg        = nullptr;
        std::wstring  username;
        std::string   token;

        // Current channel selections
        std::wstring  active_text_channel  = L"general";
        std::wstring  active_voice_channel;

        // Text channel content  (channel → lines)
        std::unordered_map<std::wstring, std::deque<ChatLine>> chat_log;

        // Voice channel state  (channel → users)
        std::unordered_map<std::wstring, std::vector<VoiceUser>> voice_users;
        // Lightweight user counts per voice channel (for sidebar badge)
        std::unordered_map<std::wstring, int> voice_counts;

        // Lists of available channels (received at login)
        std::vector<std::wstring> text_channels;
        std::vector<std::wstring> voice_channels;

        bool muted    = false;
        bool deafened = false;

        // Child windows
        HWND hwnd_chan_list   = nullptr;   // LISTBOX for text channels
        HWND hwnd_voice_list  = nullptr;   // LISTBOX for voice channels
        HWND hwnd_user_list   = nullptr;   // LISTBOX for voice channel users
        HWND hwnd_chat_list   = nullptr;   // LISTBOX owner-draw for chat messages
        HWND hwnd_input       = nullptr;   // EDIT for chat input
        HWND hwnd_mute        = nullptr;   // Button
        HWND hwnd_deafen      = nullptr;   // Button
        HWND hwnd_settings    = nullptr;   // Button
        HWND hwnd_status_bar  = nullptr;   // STATIC at bottom-left

        HFONT font_normal     = nullptr;
        HFONT font_small      = nullptr;
        HFONT font_bold       = nullptr;
        HFONT font_icon       = nullptr;

        static constexpr int CHAT_MAX_LINES = 300;
    };

    // ── Control IDs ───────────────────────────────────────────────────────────
    enum : int {
        IDC_CHAN_LIST    = 301,
        IDC_VOICE_LIST   = 302,
        IDC_USER_LIST    = 303,
        IDC_CHAT_LIST    = 304,
        IDC_CHAT_INPUT   = 305,
        IDC_BTN_SEND     = 306,
        IDC_BTN_MUTE     = 307,
        IDC_BTN_DEAFEN   = 308,
        IDC_BTN_SETTINGS = 309,
        IDC_STATUS_BAR   = 310,
    };

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int SIDEBAR_W    = 220;
    static constexpr int TOPBAR_H     = 44;
    static constexpr int BOTTOMBAR_H  = 56;
    static constexpr int INPUT_H      = 40;

    static State* GetState(HWND hwnd) {
        return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    static void LayoutChildren   (HWND hwnd, State* st);
    static void PopulateChannels (HWND hwnd, State* st);
    static void RefreshChatDisplay(State* st);
    static void AppendChatLine   (State* st, const ChatLine& line);
    static void UpdateVoiceUsers (HWND hwnd, State* st, const std::wstring& channel,
                                  const std::vector<VoiceUser>& users);
    static void SendChatMessage  (HWND hwnd, State* st);
    static void UpdateMuteButtons(State* st);
    static void DrawTopBar       (HWND hwnd, State* st, HDC hdc);
    static void DrawBottomBar    (HWND hwnd, State* st, HDC hdc);
    static void DrawMuteIcon     (HDC hdc, RECT r, bool muted, bool deafened);

    // Owner-draw for the chat list
    static void MeasureChatItem  (HWND hwnd, State* st, MEASUREITEMSTRUCT* mis);
    static void DrawChatItem     (HWND hwnd, State* st, DRAWITEMSTRUCT*    dis);
};
