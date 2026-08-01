#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Protocol.h  –  OnlyFrags Com
// Defines message type strings, UDP header layout and shared constants.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>

// ── Network defaults ──────────────────────────────────────────────────────────
constexpr int    DEFAULT_TCP_PORT  = 5000;
constexpr int    DEFAULT_UDP_PORT  = 5001;
constexpr char   DEFAULT_HOST[]    = "127.0.0.1";

// ── Audio / Opus defaults ─────────────────────────────────────────────────────
constexpr int    OPUS_SAMPLE_RATE  = 48000;   // Hz
constexpr int    OPUS_CHANNELS     = 1;       // mono
constexpr int    OPUS_FRAME_MS     = 20;      // ms per frame
constexpr int    OPUS_FRAME_SIZE   = OPUS_SAMPLE_RATE * OPUS_FRAME_MS / 1000; // 960
constexpr int    OPUS_MAX_PAYLOAD  = 4000;    // bytes (well above worst case)
constexpr int    DEFAULT_BITRATE   = 64000;   // bps

// ── Channel names ─────────────────────────────────────────────────────────────
constexpr int    NUM_TEXT_CHANNELS  = 2;
constexpr int    NUM_VOICE_CHANNELS = 11;

// ── Custom Windows messages (WM_APP + N) ──────────────────────────────────────
//   Sent from background threads to the UI thread via PostMessage.
constexpr UINT   WM_APP_NET_MSG        = WM_APP + 1;  // wParam=msg type, lParam=JSON*
constexpr UINT   WM_APP_CONNECTED      = WM_APP + 2;
constexpr UINT   WM_APP_DISCONNECTED   = WM_APP + 3;
constexpr UINT   WM_APP_LOGIN_OK       = WM_APP + 4;  // lParam = new std::wstring* (username)
constexpr UINT   WM_APP_LOGIN_FAIL     = WM_APP + 5;  // lParam = new std::wstring* (message)
constexpr UINT   WM_APP_REGISTER_OK    = WM_APP + 6;
constexpr UINT   WM_APP_REGISTER_FAIL  = WM_APP + 7;  // lParam = new std::wstring* (message)
constexpr UINT   WM_APP_CHAT_MSG       = WM_APP + 8;  // lParam = ChatMessage*
constexpr UINT   WM_APP_VOICE_STATE    = WM_APP + 9;  // lParam = VoiceState*
constexpr UINT   WM_APP_VOICE_COUNT    = WM_APP + 10; // lParam = VoiceCountMsg*
constexpr UINT   WM_APP_JOINED_VOICE   = WM_APP + 11; // lParam = new std::wstring* (channel)
constexpr UINT   WM_APP_LEFT_VOICE     = WM_APP + 12;
constexpr UINT   WM_APP_SPEAKING       = WM_APP + 13; // wParam=speaking(bool), lParam=username*
constexpr UINT   WM_APP_THEME_CHANGED  = WM_APP + 14;
constexpr UINT   WM_APP_UDP_READY      = WM_APP + 15;

// ── UDP header ────────────────────────────────────────────────────────────────
// Layout (big-endian / network byte order):
//   token_hash4 [4]  –  first 4 bytes of SHA-256(session_token)
//   channel_id  [1]  –  index into VOICE_CHANNELS
//   sequence    [2]  –  monotonically increasing packet counter
//   timestamp   [4]  –  client clock in milliseconds (rollover OK)
// Total: 11 bytes

#pragma pack(push, 1)
struct UdpVoiceHeader {
    uint8_t  token_hash4[4];
    uint8_t  channel_id;
    uint16_t sequence;       // big-endian
    uint32_t timestamp_ms;   // big-endian
};
#pragma pack(pop)

constexpr size_t UDP_HEADER_SIZE = sizeof(UdpVoiceHeader);  // 11

// ── Structured data passed via PostMessage ────────────────────────────────────
struct ChatMessageData {
    std::wstring channel;
    std::wstring username;
    std::wstring content;
    std::wstring timestamp;
};

struct VoiceUser {
    std::wstring username;
    bool         muted    = false;
    bool         deafened = false;
    bool         speaking = false;
};

struct VoiceStateData {
    std::wstring           channel;
    std::vector<VoiceUser> users;
};

struct VoiceCountData {
    std::wstring              channel;
    int                       count = 0;
    std::vector<std::wstring> usernames;
};
