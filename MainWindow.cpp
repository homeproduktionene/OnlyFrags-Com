#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// LoginWindow.h  –  OnlyFrags Com
// Login window + embedded Register dialog (both Win32, no RC required).
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <functional>
#include "Config.h"

// Called by LoginWindow after a successful login.
using LoginCallback = std::function<void(const std::wstring& username,
                                         const std::string&  token)>;

// Called by LoginWindow when settings/host need updating.
using HostCallback  = std::function<void()>;

// ─────────────────────────────────────────────────────────────────────────────
class LoginWindow {
public:
    static HWND Create(HINSTANCE hInst,
                       AppConfig& cfg,
                       LoginCallback on_login);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK RegisterDlgProc(HWND, UINT, WPARAM, LPARAM);

    // Called from the network thread (via PostMessage) to signal result
    static void OnLoginOk (HWND, const std::wstring& username);
    static void OnLoginFail(HWND, const std::wstring& message);
    static void OnRegOk   (HWND);
    static void OnRegFail (HWND, const std::wstring& message);

private:
    struct State {
        AppConfig*    cfg        = nullptr;
        LoginCallback on_login;
        HWND          hwnd_reg   = nullptr;  // Register dialog (if open)
        bool          waiting    = false;    // request in-flight
    };

    // Control IDs
    enum : int {
        IDC_EMAIL       = 101,
        IDC_PASSWORD    = 102,
        IDC_BTN_LOGIN   = 103,
        IDC_BTN_REGISTER= 104,
        IDC_STATUS      = 105,
        IDC_HOST_LABEL  = 106,

        IDR_USERNAME    = 201,
        IDR_EMAIL       = 202,
        IDR_PASSWORD    = 203,
        IDR_INVITE      = 204,
        IDR_BTN_SUBMIT  = 205,
        IDR_BTN_CANCEL  = 206,
        IDR_STATUS      = 207,
    };

    static State* GetState(HWND hwnd) {
        return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    static void ApplyThemeToControls(HWND hwnd);
    static void SetStatus(HWND hwnd, const std::wstring& msg, bool is_error);
    static void SetRegStatus(HWND hreg, const std::wstring& msg, bool is_error);
    static void OpenRegisterDialog(HWND parent, State* st);
};
