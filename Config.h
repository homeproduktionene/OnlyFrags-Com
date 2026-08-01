// ─────────────────────────────────────────────────────────────────────────────
// Config.cpp  –  OnlyFrags Com
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>
#include "Config.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Locate config file ────────────────────────────────────────────────────────
static fs::path GetConfigPath() {
    // Prefer %APPDATA%\OnlyFragsCom\
    wchar_t appdata[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        fs::path dir = fs::path(appdata) / L"OnlyFragsCom";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir / L"config.json";
    }
    // Fallback: next to the EXE
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path() / L"config.json";
}

// ── Serialisation helpers ─────────────────────────────────────────────────────
static ThemeMode ThemeFromInt(int v) {
    switch (v) {
        case 1: return ThemeMode::Light;
        case 2: return ThemeMode::Dark;
        default: return ThemeMode::Auto;
    }
}
static VoiceMode VoiceModeFromInt(int v) {
    return (v == 1) ? VoiceMode::PushToTalk : VoiceMode::VoiceActivation;
}

// ── Public API ────────────────────────────────────────────────────────────────
AppConfig Config_Load() {
    AppConfig cfg{};
    fs::path path = GetConfigPath();

    if (!fs::exists(path))
        return cfg;   // return defaults

    try {
        std::ifstream f(path);
        json j;
        f >> j;

        // Network
        if (j.contains("host"))      cfg.host      = j["host"].get<std::string>();
        if (j.contains("tcp_port"))  cfg.tcp_port  = j["tcp_port"].get<int>();
        if (j.contains("udp_port"))  cfg.udp_port  = j["udp_port"].get<int>();
        if (j.contains("verify_tls"))cfg.verify_tls= j["verify_tls"].get<bool>();

        // Audio
        if (j.contains("mic_device"))    cfg.mic_device     = j["mic_device"].get<std::string>();
        if (j.contains("out_device"))    cfg.out_device     = j["out_device"].get<std::string>();
        if (j.contains("mic_volume"))    cfg.mic_volume     = j["mic_volume"].get<float>();
        if (j.contains("out_volume"))    cfg.out_volume     = j["out_volume"].get<float>();
        if (j.contains("bitrate"))       cfg.bitrate        = j["bitrate"].get<int>();
        if (j.contains("voice_mode"))    cfg.voice_mode     = VoiceModeFromInt(j["voice_mode"].get<int>());
        if (j.contains("ptt_key"))       cfg.ptt_key        = j["ptt_key"].get<int>();
        if (j.contains("vad_threshold")) cfg.vad_threshold  = j["vad_threshold"].get<float>();
        if (j.contains("echo_cancel"))   cfg.echo_cancel    = j["echo_cancel"].get<bool>();
        if (j.contains("noise_suppress"))cfg.noise_suppress = j["noise_suppress"].get<bool>();
        if (j.contains("auto_gain"))     cfg.auto_gain      = j["auto_gain"].get<bool>();

        // Appearance
        if (j.contains("theme"))         cfg.theme          = ThemeFromInt(j["theme"].get<int>());

        // General
        if (j.contains("start_minimized"))     cfg.start_minimized     = j["start_minimized"].get<bool>();
        if (j.contains("start_with_windows"))  cfg.start_with_windows  = j["start_with_windows"].get<bool>();
        if (j.contains("notifications"))       cfg.notifications       = j["notifications"].get<bool>();
        if (j.contains("language"))            cfg.language            = j["language"].get<std::string>();

    } catch (const std::exception&) {
        // Corrupt config – return defaults
        return AppConfig{};
    }
    return cfg;
}

void Config_Save(const AppConfig& cfg) {
    json j;

    // Network
    j["host"]       = cfg.host;
    j["tcp_port"]   = cfg.tcp_port;
    j["udp_port"]   = cfg.udp_port;
    j["verify_tls"] = cfg.verify_tls;

    // Audio
    j["mic_device"]    = cfg.mic_device;
    j["out_device"]    = cfg.out_device;
    j["mic_volume"]    = cfg.mic_volume;
    j["out_volume"]    = cfg.out_volume;
    j["bitrate"]       = cfg.bitrate;
    j["voice_mode"]    = static_cast<int>(cfg.voice_mode);
    j["ptt_key"]       = cfg.ptt_key;
    j["vad_threshold"] = cfg.vad_threshold;
    j["echo_cancel"]   = cfg.echo_cancel;
    j["noise_suppress"]= cfg.noise_suppress;
    j["auto_gain"]     = cfg.auto_gain;

    // Appearance
    j["theme"]              = static_cast<int>(cfg.theme);

    // General
    j["start_minimized"]    = cfg.start_minimized;
    j["start_with_windows"] = cfg.start_with_windows;
    j["notifications"]      = cfg.notifications;
    j["language"]           = cfg.language;

    try {
        fs::path path = GetConfigPath();
        std::ofstream f(path);
        f << j.dump(2);
    } catch (const std::exception&) {
        // Best-effort save – ignore failures silently
    }
}
