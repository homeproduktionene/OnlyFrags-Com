#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// VoiceEngine.h  –  OnlyFrags Com
// WASAPI audio capture/render + Opus codec + UDP voice relay.
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <deque>
#include <cstdint>

#include <opus/opus.h>
#include "Config.h"
#include "Protocol.h"

// ── Per-sender jitter buffer ──────────────────────────────────────────────────
// Stores decoded 16-bit mono frames waiting for playback.
struct JitterBuffer {
    static constexpr int MAX_FRAMES = 12;          // 240 ms max
    static constexpr int TARGET_DELAY = 2;         // 40 ms target delay

    std::deque<std::vector<int16_t>> frames;
    std::mutex                        mtx;
    uint32_t                          last_ts   = 0;
    bool                              primed    = false;

    void Push(const int16_t* pcm, int samples);
    // Returns false when buffer is dry (insert silence).
    bool Pop (int16_t* out,  int samples);
};

// ─────────────────────────────────────────────────────────────────────────────
class VoiceEngine {
public:
    VoiceEngine();
    ~VoiceEngine();

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    bool Init(HWND hwnd, const AppConfig& cfg);
    void Shutdown();

    // ── Channel management ────────────────────────────────────────────────────
    void JoinChannel(const std::string& channel_name, uint8_t channel_id);
    void LeaveChannel();

    // ── Auth ──────────────────────────────────────────────────────────────────
    void SetToken(const std::string& hex_token); // 64-char hex string

    // ── UDP endpoint ──────────────────────────────────────────────────────────
    // Call after TCP login_ok + register_udp has been acknowledged.
    // Returns the local UDP port that was bound.
    int  BindUdp();
    void SetServerEndpoint(const std::string& host, int udp_port);
    int  GetLocalUdpPort() const { return m_local_udp_port; }

    // ── Controls ──────────────────────────────────────────────────────────────
    void SetMuted   (bool m);
    void SetDeafened(bool d);
    bool IsMuted    () const;
    bool IsDeafened () const;

    // ── Audio settings (live-update) ──────────────────────────────────────────
    void SetMicVolume (float v);   // 0.0 – 2.0
    void SetOutVolume (float v);
    void SetBitrate   (int bps);   // 32000 – 128000
    void SetVoiceMode (VoiceMode m, int ptt_key = 0);
    void SetVadThreshold(float t);

    // ── Level metering (for UI) ────────────────────────────────────────────────
    float GetInputLevel()  const;  // 0.0 – 1.0 RMS
    float GetOutputLevel() const;

    // ── Device enumeration ────────────────────────────────────────────────────
    std::vector<std::wstring> EnumInputDevices();
    std::vector<std::wstring> EnumOutputDevices();

    // ── Microphone test (plays back captured audio for 5 s) ──────────────────
    void StartMicTest();
    void StopMicTest();

private:
    // ── Thread workers ────────────────────────────────────────────────────────
    void CaptureThread();
    void RenderThread ();
    void UdpRecvThread();

    // ── WASAPI helpers ────────────────────────────────────────────────────────
    bool InitCapture (const std::string& device_id);
    bool InitRender  (const std::string& device_id);
    void CloseCapture();
    void CloseRender ();

    // ── Audio format conversion ───────────────────────────────────────────────
    // Convert whatever WASAPI gives us to 48 kHz mono int16.
    void ConvertToOpusFormat(const BYTE* src, UINT32 frames_avail,
                             WAVEFORMATEX* wfx, std::vector<int16_t>& out);
    // Convert 48 kHz mono int16 to WASAPI render format.
    void ConvertFromOpusFormat(const int16_t* src, int src_frames,
                               WAVEFORMATEX* wfx, std::vector<BYTE>& out);

    // ── Members ───────────────────────────────────────────────────────────────
    HWND               m_hwnd          = nullptr;

    // Config snapshot (protected by m_cfg_mtx)
    std::mutex         m_cfg_mtx;
    AppConfig          m_cfg;

    // Opus
    OpusEncoder*       m_encoder       = nullptr;
    OpusDecoder*       m_decoder       = nullptr;  // own decoder for mic test

    // WASAPI – capture
    IMMDevice*         m_cap_dev       = nullptr;
    IAudioClient*      m_cap_client    = nullptr;
    IAudioCaptureClient* m_cap_svc     = nullptr;
    WAVEFORMATEX*      m_cap_wfx       = nullptr;
    HANDLE             m_cap_event     = nullptr;

    // WASAPI – render
    IMMDevice*         m_rnd_dev       = nullptr;
    IAudioClient*      m_rnd_client    = nullptr;
    IAudioRenderClient* m_rnd_svc      = nullptr;
    WAVEFORMATEX*      m_rnd_wfx       = nullptr;
    HANDLE             m_rnd_event     = nullptr;

    // UDP
    SOCKET             m_udp_sock      = INVALID_SOCKET;
    int                m_local_udp_port= 0;
    std::string        m_srv_host;
    int                m_srv_udp_port  = 5001;
    sockaddr_in        m_srv_addr      = {};

    // Session
    uint8_t            m_token_hash4[4]= {};
    uint8_t            m_channel_id    = 0;
    std::atomic<bool>  m_in_channel   { false };

    // State
    std::atomic<bool>  m_muted        { false };
    std::atomic<bool>  m_deafened     { false };
    std::atomic<bool>  m_mic_test     { false };
    std::atomic<float> m_in_level     { 0.0f };
    std::atomic<float> m_out_level    { 0.0f };
    uint16_t           m_seq           = 0;

    // Threads
    std::thread        m_cap_thread;
    std::thread        m_rnd_thread;
    std::thread        m_udp_thread;
    std::atomic<bool>  m_running      { false };

    // Per-sender jitter buffers  key = 4-byte token hash as uint32
    std::mutex         m_jb_mtx;
    std::unordered_map<uint32_t, JitterBuffer> m_jitter;

    // Per-sender Opus decoders   key = same uint32
    std::mutex         m_dec_mtx;
    std::unordered_map<uint32_t, OpusDecoder*> m_decoders;
};
