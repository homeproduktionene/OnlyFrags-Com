#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Config.h  –  OnlyFrags Com
// Persistent application settings (JSON, stored next to the EXE).
// ─────────────────────────────────────────────────────────────────────────────

#include <string>

enum class ThemeMode {
    Auto  = 0,   // follow Windows setting
    Light = 1,
    Dark  = 2,
};

enum class VoiceMode {
    VoiceActivation = 0,
    PushToTalk      = 1,
};

struct AppConfig {
    // ── Network ──────────────────────────────────────────────────────────────
    std::string host          = "127.0.0.1";
    int         tcp_port      = 5000;
    int         udp_port      = 5001;
    bool        verify_tls    = false;   // set true when using a real CA cert

    // ── Audio ─────────────────────────────────────────────────────────────────
    std::string mic_device    = "";      // empty = system default
    std::string out_device    = "";      // empty = system default
    float       mic_volume    = 1.0f;   // 0.0 – 2.0
    float       out_volume    = 1.0f;   // 0.0 – 2.0
    int         bitrate       = 64000;  // Opus bps
    VoiceMode   voice_mode    = VoiceMode::VoiceActivation;
    int         ptt_key       = 0x11;   // VK_CONTROL by default
    float       vad_threshold = 0.02f;  // RMS gate (0.0 – 1.0)
    bool        echo_cancel   = true;
    bool        noise_suppress= true;
    bool        auto_gain     = true;

    // ── Appearance ────────────────────────────────────────────────────────────
    ThemeMode   theme         = ThemeMode::Auto;

    // ── General ───────────────────────────────────────────────────────────────
    bool        start_minimized    = false;
    bool        start_with_windows = false;
    bool        notifications      = true;
    std::string language           = "de";
};

// Load config from %APPDATA%\OnlyFragsCom\config.json (or next to EXE as fallback).
AppConfig Config_Load();

// Persist current config.
void Config_Save(const AppConfig& cfg);
