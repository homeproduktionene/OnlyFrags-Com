// ─────────────────────────────────────────────────────────────────────────────
// VoiceEngine.cpp  –  OnlyFrags Com
// WASAPI shared-mode audio  |  Opus codec  |  UDP relay
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _USE_MATH_DEFINES
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "ws2_32.lib")

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <sstream>
#include <stdexcept>

#include <opus/opus.h>
#include "VoiceEngine.h"
#include "Protocol.h"

// ── SHA-256 mini-impl for token hashing (avoids OpenSSL dep in this TU) ──────
// We use the Windows BCrypt API for hashing.
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

static void sha256_first4(const std::string& hex_token, uint8_t out[4]) {
    // Convert hex string → raw bytes
    std::vector<uint8_t> raw;
    raw.reserve(hex_token.size() / 2);
    for (size_t i = 0; i + 1 < hex_token.size(); i += 2) {
        raw.push_back(static_cast<uint8_t>(
            std::stoi(hex_token.substr(i, 2), nullptr, 16)));
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashLen = 32, cbData = 0;
    std::vector<uint8_t> hashObj(256), digest(hashLen);

    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    DWORD objLen = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbData, 0);
    hashObj.resize(objLen);
    BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0);
    BCryptHashData(hHash, raw.data(), static_cast<ULONG>(raw.size()), 0);
    BCryptFinishHash(hHash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    memcpy(out, digest.data(), 4);
}

// ── Helpers: byte-swap for big-endian UDP header ──────────────────────────────
static uint16_t hton16(uint16_t v) { return _byteswap_ushort(v); }
static uint32_t hton32(uint32_t v) { return _byteswap_ulong(v); }
static uint16_t ntoh16(uint16_t v) { return _byteswap_ushort(v); }
static uint32_t ntoh32(uint32_t v) { return _byteswap_ulong(v); }

// ── RMS level computation ─────────────────────────────────────────────────────
static float ComputeRms(const int16_t* data, int count) {
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return static_cast<float>(std::sqrt(sum / count) / 32768.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// JitterBuffer
// ─────────────────────────────────────────────────────────────────────────────
void JitterBuffer::Push(const int16_t* pcm, int samples) {
    std::lock_guard<std::mutex> lk(mtx);
    if (static_cast<int>(frames.size()) >= MAX_FRAMES)
        frames.pop_front();   // discard oldest – prefer latency over quality
    frames.emplace_back(pcm, pcm + samples);
}

bool JitterBuffer::Pop(int16_t* out, int samples) {
    std::lock_guard<std::mutex> lk(mtx);
    if (!primed) {
        if (static_cast<int>(frames.size()) < TARGET_DELAY)
            return false;   // accumulate initial delay
        primed = true;
    }
    if (frames.empty()) return false;
    const auto& f = frames.front();
    int copy = std::min(samples, static_cast<int>(f.size()));
    memcpy(out, f.data(), copy * sizeof(int16_t));
    if (copy < samples)
        memset(out + copy, 0, (samples - copy) * sizeof(int16_t));
    frames.pop_front();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VoiceEngine – construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
VoiceEngine::VoiceEngine() {
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);
}

VoiceEngine::~VoiceEngine() {
    Shutdown();
    WSACleanup();
}

bool VoiceEngine::Init(HWND hwnd, const AppConfig& cfg) {
    m_hwnd = hwnd;
    {
        std::lock_guard<std::mutex> lk(m_cfg_mtx);
        m_cfg = cfg;
    }

    // ── Opus encoder ─────────────────────────────────────────────────────────
    int err = 0;
    m_encoder = opus_encoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS,
                                    OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !m_encoder) return false;

    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(cfg.bitrate));
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(m_encoder, OPUS_SET_DTX(1));
    if (cfg.noise_suppress)
        opus_encoder_ctl(m_encoder, OPUS_SET_NOISE_REDUCTION_STRENGTH_REQUEST, 4);
    if (cfg.echo_cancel)
        opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    if (cfg.auto_gain)
        opus_encoder_ctl(m_encoder, OPUS_SET_GAIN(0));

    // ── WASAPI ───────────────────────────────────────────────────────────────
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!InitCapture(cfg.mic_device)) return false;
    if (!InitRender (cfg.out_device)) return false;

    m_running.store(true);
    m_cap_thread = std::thread(&VoiceEngine::CaptureThread, this);
    m_rnd_thread = std::thread(&VoiceEngine::RenderThread,  this);
    m_udp_thread = std::thread(&VoiceEngine::UdpRecvThread, this);

    return true;
}

void VoiceEngine::Shutdown() {
    m_running.store(false);

    // Wake events
    if (m_cap_event) SetEvent(m_cap_event);
    if (m_rnd_event) SetEvent(m_rnd_event);

    // Close UDP socket to unblock recvfrom
    if (m_udp_sock != INVALID_SOCKET) {
        closesocket(m_udp_sock);
        m_udp_sock = INVALID_SOCKET;
    }

    if (m_cap_thread.joinable()) m_cap_thread.join();
    if (m_rnd_thread.joinable()) m_rnd_thread.join();
    if (m_udp_thread.joinable()) m_udp_thread.join();

    CloseCapture();
    CloseRender();

    if (m_encoder) { opus_encoder_destroy(m_encoder); m_encoder = nullptr; }
    if (m_decoder) { opus_decoder_destroy(m_decoder); m_decoder = nullptr; }

    {
        std::lock_guard<std::mutex> lk(m_dec_mtx);
        for (auto& [k, d] : m_decoders) opus_decoder_destroy(d);
        m_decoders.clear();
    }

    CoUninitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// WASAPI init / close
// ─────────────────────────────────────────────────────────────────────────────
static IMMDevice* GetDeviceByFriendlyName(IMMDeviceEnumerator* enumerator,
                                           EDataFlow flow,
                                           const std::string& friendly_name_u8) {
    IMMDeviceCollection* coll = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
        return nullptr;

    UINT count = 0;
    coll->GetCount(&count);

    std::wstring target(friendly_name_u8.begin(), friendly_name_u8.end());

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        coll->Item(i, &dev);
        if (!dev) continue;

        IPropertyStore* props = nullptr;
        dev->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (props) {
            props->GetValue(PKEY_Device_FriendlyName, &pv);
            props->Release();
        }
        bool match = (pv.vt == VT_LPWSTR && target == pv.pwszVal);
        PropVariantClear(&pv);

        if (match) { coll->Release(); return dev; }
        dev->Release();
    }
    coll->Release();
    return nullptr;
}

bool VoiceEngine::InitCapture(const std::string& device_id) {
    IMMDeviceEnumerator* enumerator = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));

    if (!device_id.empty())
        m_cap_dev = GetDeviceByFriendlyName(enumerator, eCapture, device_id);

    if (!m_cap_dev)
        enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &m_cap_dev);

    enumerator->Release();
    if (!m_cap_dev) return false;

    m_cap_dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                        nullptr, reinterpret_cast<void**>(&m_cap_client));

    m_cap_client->GetMixFormat(&m_cap_wfx);

    REFERENCE_TIME buf_dur = 10000000LL * OPUS_FRAME_MS / 1000 * 2; // 2 frames
    m_cap_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HRESULT hr = m_cap_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        buf_dur, 0, m_cap_wfx, nullptr);
    if (FAILED(hr)) return false;

    m_cap_client->SetEventHandle(m_cap_event);
    m_cap_client->GetService(__uuidof(IAudioCaptureClient),
                             reinterpret_cast<void**>(&m_cap_svc));
    m_cap_client->Start();
    return (m_cap_svc != nullptr);
}

bool VoiceEngine::InitRender(const std::string& device_id) {
    IMMDeviceEnumerator* enumerator = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                     __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));

    if (!device_id.empty())
        m_rnd_dev = GetDeviceByFriendlyName(enumerator, eRender, device_id);

    if (!m_rnd_dev)
        enumerator->GetDefaultAudioEndpoint(eRender, eCommunications, &m_rnd_dev);

    enumerator->Release();
    if (!m_rnd_dev) return false;

    m_rnd_dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                        nullptr, reinterpret_cast<void**>(&m_rnd_client));

    m_rnd_client->GetMixFormat(&m_rnd_wfx);

    REFERENCE_TIME buf_dur = 10000000LL * OPUS_FRAME_MS / 1000 * 4;
    m_rnd_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    HRESULT hr = m_rnd_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        buf_dur, 0, m_rnd_wfx, nullptr);
    if (FAILED(hr)) return false;

    m_rnd_client->SetEventHandle(m_rnd_event);
    m_rnd_client->GetService(__uuidof(IAudioRenderClient),
                             reinterpret_cast<void**>(&m_rnd_svc));
    m_rnd_client->Start();
    return (m_rnd_svc != nullptr);
}

void VoiceEngine::CloseCapture() {
    if (m_cap_client) { m_cap_client->Stop(); m_cap_client->Release(); m_cap_client = nullptr; }
    if (m_cap_svc)    { m_cap_svc->Release(); m_cap_svc = nullptr; }
    if (m_cap_dev)    { m_cap_dev->Release(); m_cap_dev = nullptr; }
    if (m_cap_wfx)    { CoTaskMemFree(m_cap_wfx); m_cap_wfx = nullptr; }
    if (m_cap_event)  { CloseHandle(m_cap_event); m_cap_event = nullptr; }
}

void VoiceEngine::CloseRender() {
    if (m_rnd_client) { m_rnd_client->Stop(); m_rnd_client->Release(); m_rnd_client = nullptr; }
    if (m_rnd_svc)    { m_rnd_svc->Release(); m_rnd_svc = nullptr; }
    if (m_rnd_dev)    { m_rnd_dev->Release(); m_rnd_dev = nullptr; }
    if (m_rnd_wfx)    { CoTaskMemFree(m_rnd_wfx); m_rnd_wfx = nullptr; }
    if (m_rnd_event)  { CloseHandle(m_rnd_event); m_rnd_event = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio format conversion
// ─────────────────────────────────────────────────────────────────────────────
void VoiceEngine::ConvertToOpusFormat(const BYTE* src, UINT32 frames,
                                       WAVEFORMATEX* wfx,
                                       std::vector<int16_t>& out) {
    if (!src || frames == 0 || !wfx) return;

    int channels    = wfx->nChannels;
    int sample_rate = wfx->nSamplesPerSec;
    bool is_float   = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                      (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                       reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat ==
                       KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    // Step 1: Deinterleave to mono int16 at device sample rate
    std::vector<int16_t> mono(frames);
    for (UINT32 i = 0; i < frames; ++i) {
        float sample = 0.0f;
        if (is_float) {
            const float* fp = reinterpret_cast<const float*>(src) + i * channels;
            for (int c = 0; c < channels; ++c) sample += fp[c];
            sample /= channels;
        } else {
            const int16_t* ip = reinterpret_cast<const int16_t*>(src) + i * channels;
            float s = 0.0f;
            for (int c = 0; c < channels; ++c) s += ip[c];
            sample = s / channels / 32768.0f;
        }
        // Apply mic volume
        float vol;
        {
            std::lock_guard<std::mutex> lk(m_cfg_mtx);
            vol = m_cfg.mic_volume;
        }
        sample = std::clamp(sample * vol, -1.0f, 1.0f);
        mono[i] = static_cast<int16_t>(sample * 32767.0f);
    }

    // Step 2: Resample to 48000 Hz if necessary
    if (sample_rate == OPUS_SAMPLE_RATE) {
        out = std::move(mono);
        return;
    }
    double ratio = static_cast<double>(OPUS_SAMPLE_RATE) / sample_rate;
    int out_count = static_cast<int>(frames * ratio);
    out.resize(out_count);
    for (int i = 0; i < out_count; ++i) {
        double src_pos = i / ratio;
        int    idx0    = static_cast<int>(src_pos);
        int    idx1    = std::min(idx0 + 1, static_cast<int>(frames) - 1);
        float  frac    = static_cast<float>(src_pos - idx0);
        out[i] = static_cast<int16_t>(
            mono[idx0] * (1.0f - frac) + mono[idx1] * frac);
    }
}

void VoiceEngine::ConvertFromOpusFormat(const int16_t* src, int src_frames,
                                         WAVEFORMATEX* wfx,
                                         std::vector<BYTE>& out) {
    if (!src || src_frames == 0 || !wfx) return;

    int channels    = wfx->nChannels;
    int sample_rate = wfx->nSamplesPerSec;
    bool is_float   = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
                      (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                       reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat ==
                       KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    // Step 1: Resample from 48000 to device rate
    int dev_frames;
    std::vector<int16_t> resampled;
    if (sample_rate == OPUS_SAMPLE_RATE) {
        dev_frames = src_frames;
        resampled.assign(src, src + src_frames);
    } else {
        double ratio = static_cast<double>(sample_rate) / OPUS_SAMPLE_RATE;
        dev_frames = static_cast<int>(src_frames * ratio);
        resampled.resize(dev_frames);
        for (int i = 0; i < dev_frames; ++i) {
            double src_pos = i / ratio;
            int    idx0    = static_cast<int>(src_pos);
            int    idx1    = std::min(idx0 + 1, src_frames - 1);
            float  frac    = static_cast<float>(src_pos - idx0);
            resampled[i]   = static_cast<int16_t>(
                src[idx0] * (1.0f - frac) + src[idx1] * frac);
        }
    }

    float vol;
    {
        std::lock_guard<std::mutex> lk(m_cfg_mtx);
        vol = m_cfg.out_volume;
    }

    // Step 2: Interleave mono → n-channel, convert to device format
    int bytes_per_sample = is_float ? 4 : 2;
    out.resize(dev_frames * channels * bytes_per_sample);
    BYTE* dst = out.data();

    for (int i = 0; i < dev_frames; ++i) {
        float s = std::clamp(resampled[i] / 32768.0f * vol, -1.0f, 1.0f);
        for (int c = 0; c < channels; ++c) {
            if (is_float) {
                *reinterpret_cast<float*>(dst) = s;
                dst += 4;
            } else {
                *reinterpret_cast<int16_t*>(dst) = static_cast<int16_t>(s * 32767.0f);
                dst += 2;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture thread  →  Opus encode  →  UDP send
// ─────────────────────────────────────────────────────────────────────────────
void VoiceEngine::CaptureThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_idx = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);

    std::vector<int16_t> opus_in;
    std::vector<int16_t> accum;     // accumulate until we have a full Opus frame
    accum.reserve(OPUS_FRAME_SIZE * 4);

    uint8_t packet[UDP_HEADER_SIZE + OPUS_MAX_PAYLOAD];

    while (m_running.load()) {
        DWORD wait = WaitForSingleObject(m_cap_event, 100);
        if (!m_running.load()) break;
        if (wait != WAIT_OBJECT_0) continue;
        if (!m_cap_svc) continue;

        UINT32   pkt_frames = 0;
        DWORD    flags      = 0;
        UINT64   dev_pos    = 0;
        BYTE*    data       = nullptr;

        while (SUCCEEDED(m_cap_svc->GetNextPacketSize(&pkt_frames)) && pkt_frames > 0) {
            if (FAILED(m_cap_svc->GetBuffer(&data, &pkt_frames, &flags, &dev_pos, nullptr)))
                break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                ConvertToOpusFormat(data, pkt_frames, m_cap_wfx, opus_in);
                accum.insert(accum.end(), opus_in.begin(), opus_in.end());
            } else {
                // Insert silence
                accum.resize(accum.size() + pkt_frames, 0);
            }

            m_cap_svc->ReleaseBuffer(pkt_frames);

            // Process full Opus frames
            while (static_cast<int>(accum.size()) >= OPUS_FRAME_SIZE) {
                const int16_t* frame = accum.data();

                // Update input level meter
                m_in_level.store(ComputeRms(frame, OPUS_FRAME_SIZE));

                // Voice mode gate
                bool should_send = false;
                {
                    std::lock_guard<std::mutex> lk(m_cfg_mtx);
                    if (m_cfg.voice_mode == VoiceMode::PushToTalk) {
                        should_send = (GetAsyncKeyState(m_cfg.ptt_key) & 0x8000) != 0;
                    } else {
                        should_send = (m_in_level.load() >= m_cfg.vad_threshold);
                    }
                }

                if (!m_muted.load() && m_in_channel.load() && should_send &&
                    m_udp_sock != INVALID_SOCKET) {

                    // Opus encode
                    uint8_t* payload = packet + UDP_HEADER_SIZE;
                    int encoded = opus_encode(m_encoder, frame, OPUS_FRAME_SIZE,
                                             payload, OPUS_MAX_PAYLOAD);
                    if (encoded > 0) {
                        // Build header
                        auto* hdr = reinterpret_cast<UdpVoiceHeader*>(packet);
                        memcpy(hdr->token_hash4, m_token_hash4, 4);
                        hdr->channel_id  = m_channel_id;
                        hdr->sequence    = hton16(m_seq++);
                        hdr->timestamp_ms= hton32(static_cast<uint32_t>(
                            GetTickCount64() & 0xFFFFFFFF));

                        sendto(m_udp_sock,
                               reinterpret_cast<char*>(packet),
                               static_cast<int>(UDP_HEADER_SIZE + encoded),
                               0,
                               reinterpret_cast<sockaddr*>(&m_srv_addr),
                               sizeof(m_srv_addr));
                    }
                }

                // Mic test playback: push raw PCM into render jitter buffer
                if (m_mic_test.load()) {
                    uint32_t self_key = *reinterpret_cast<const uint32_t*>(m_token_hash4);
                    std::lock_guard<std::mutex> lk(m_jb_mtx);
                    m_jitter[self_key].Push(frame, OPUS_FRAME_SIZE);
                }

                accum.erase(accum.begin(), accum.begin() + OPUS_FRAME_SIZE);
            }
        }
    }

    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP receive thread  →  Opus decode  →  jitter buffer
// ─────────────────────────────────────────────────────────────────────────────
void VoiceEngine::UdpRecvThread() {
    uint8_t buf[UDP_HEADER_SIZE + OPUS_MAX_PAYLOAD];
    int16_t  decoded[OPUS_FRAME_SIZE];

    while (m_running.load()) {
        if (m_udp_sock == INVALID_SOCKET) { Sleep(20); continue; }

        sockaddr_in from{};
        int from_len = sizeof(from);
        int n = recvfrom(m_udp_sock, reinterpret_cast<char*>(buf), sizeof(buf),
                         0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n <= static_cast<int>(UDP_HEADER_SIZE)) continue;
        if (m_deafened.load() || !m_running.load()) continue;

        const auto* hdr    = reinterpret_cast<const UdpVoiceHeader*>(buf);
        uint32_t    sender = *reinterpret_cast<const uint32_t*>(hdr->token_hash4);

        // Get or create per-sender decoder
        OpusDecoder* dec = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_dec_mtx);
            auto it = m_decoders.find(sender);
            if (it == m_decoders.end()) {
                int err = 0;
                dec = opus_decoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS, &err);
                if (err == OPUS_OK && dec)
                    m_decoders[sender] = dec;
            } else {
                dec = it->second;
            }
        }
        if (!dec) continue;

        const uint8_t* payload     = buf + UDP_HEADER_SIZE;
        int            payload_len = n   - static_cast<int>(UDP_HEADER_SIZE);

        int out_frames = opus_decode(dec, payload, payload_len,
                                     decoded, OPUS_FRAME_SIZE, 0);
        if (out_frames > 0) {
            std::lock_guard<std::mutex> lk(m_jb_mtx);
            m_jitter[sender].Push(decoded, out_frames);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render thread  →  mix jitter buffers  →  WASAPI render
// ─────────────────────────────────────────────────────────────────────────────
void VoiceEngine::RenderThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task_idx = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);

    UINT32 buf_frames = 0;
    if (m_rnd_client) m_rnd_client->GetBufferSize(&buf_frames);

    std::vector<int16_t> mix(OPUS_FRAME_SIZE, 0);
    std::vector<int16_t> frame(OPUS_FRAME_SIZE, 0);
    std::vector<BYTE>    out_bytes;

    while (m_running.load()) {
        DWORD wait = WaitForSingleObject(m_rnd_event, 100);
        if (!m_running.load()) break;
        if (wait != WAIT_OBJECT_0) continue;
        if (!m_rnd_client || !m_rnd_svc) continue;

        UINT32 padding = 0;
        m_rnd_client->GetCurrentPadding(&padding);
        UINT32 frames_available = buf_frames - padding;
        if (frames_available == 0) continue;

        BYTE* render_buf = nullptr;
        if (FAILED(m_rnd_svc->GetBuffer(frames_available, &render_buf))) continue;

        // How many Opus frames fit in the available render buffer?
        // We render in OPUS_FRAME_SIZE chunks
        // Convert available render frames to Opus frames
        int device_sr = m_rnd_wfx->nSamplesPerSec;
        int opus_frames_needed = static_cast<int>(
            static_cast<double>(frames_available) * OPUS_SAMPLE_RATE / device_sr);
        // Round to Opus frame boundary
        opus_frames_needed = (opus_frames_needed / OPUS_FRAME_SIZE) * OPUS_FRAME_SIZE;
        if (opus_frames_needed < OPUS_FRAME_SIZE) {
            m_rnd_svc->ReleaseBuffer(frames_available, AUDCLNT_BUFFERFLAGS_SILENT);
            continue;
        }

        std::vector<int16_t> all_mixed(opus_frames_needed, 0);

        // Mix all jitter buffers
        {
            std::lock_guard<std::mutex> lk(m_jb_mtx);
            for (auto& [key, jb] : m_jitter) {
                for (int offset = 0; offset < opus_frames_needed; offset += OPUS_FRAME_SIZE) {
                    bool got = jb.Pop(frame.data(), OPUS_FRAME_SIZE);
                    if (got) {
                        for (int i = 0; i < OPUS_FRAME_SIZE; ++i) {
                            int32_t sum = all_mixed[offset + i] + frame[i];
                            all_mixed[offset + i] = static_cast<int16_t>(
                                std::clamp(sum, -32768, 32767));
                        }
                    }
                }
            }
        }

        // Level meter
        m_out_level.store(ComputeRms(all_mixed.data(), static_cast<int>(all_mixed.size())));

        // Convert to device format
        ConvertFromOpusFormat(all_mixed.data(), static_cast<int>(all_mixed.size()),
                              m_rnd_wfx, out_bytes);

        UINT32 bytes_per_frame = m_rnd_wfx->nBlockAlign;
        UINT32 out_dev_frames  = static_cast<UINT32>(out_bytes.size() / bytes_per_frame);
        UINT32 copy_frames     = std::min(out_dev_frames, frames_available);

        memcpy(render_buf, out_bytes.data(), copy_frames * bytes_per_frame);
        m_rnd_svc->ReleaseBuffer(copy_frames, 0);
    }

    if (task) AvRevertMmThreadCharacteristics(task);
    CoUninitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// UDP socket management
// ─────────────────────────────────────────────────────────────────────────────
int VoiceEngine::BindUdp() {
    if (m_udp_sock != INVALID_SOCKET) {
        closesocket(m_udp_sock);
        m_udp_sock = INVALID_SOCKET;
    }
    m_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udp_sock == INVALID_SOCKET) return 0;

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = 0;   // OS picks a free port

    if (bind(m_udp_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(m_udp_sock);
        m_udp_sock = INVALID_SOCKET;
        return 0;
    }

    int len = sizeof(addr);
    getsockname(m_udp_sock, reinterpret_cast<sockaddr*>(&addr),
                reinterpret_cast<socklen_t*>(&len));
    m_local_udp_port = ntohs(addr.sin_port);

    // Set receive timeout so UdpRecvThread can check m_running periodically
    DWORD timeout = 200;
    setsockopt(m_udp_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<char*>(&timeout), sizeof(timeout));

    return m_local_udp_port;
}

void VoiceEngine::SetServerEndpoint(const std::string& host, int udp_port) {
    m_srv_host     = host;
    m_srv_udp_port = udp_port;

    memset(&m_srv_addr, 0, sizeof(m_srv_addr));
    m_srv_addr.sin_family = AF_INET;
    m_srv_addr.sin_port   = htons(static_cast<uint16_t>(udp_port));
    inet_pton(AF_INET, host.c_str(), &m_srv_addr.sin_addr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public controls
// ─────────────────────────────────────────────────────────────────────────────
void VoiceEngine::SetToken(const std::string& hex_token) {
    sha256_first4(hex_token, m_token_hash4);
}

void VoiceEngine::JoinChannel(const std::string& channel_name, uint8_t channel_id) {
    m_channel_id = channel_id;
    m_in_channel.store(true);
    // Clear stale jitter buffers
    std::lock_guard<std::mutex> lk(m_jb_mtx);
    m_jitter.clear();
}

void VoiceEngine::LeaveChannel() {
    m_in_channel.store(false);
    std::lock_guard<std::mutex> lk(m_jb_mtx);
    m_jitter.clear();
}

void VoiceEngine::SetMuted(bool m)    { m_muted.store(m); }
void VoiceEngine::SetDeafened(bool d) { m_deafened.store(d); }
bool VoiceEngine::IsMuted()    const  { return m_muted.load(); }
bool VoiceEngine::IsDeafened() const  { return m_deafened.load(); }

void VoiceEngine::SetMicVolume(float v)  {
    std::lock_guard<std::mutex> lk(m_cfg_mtx);
    m_cfg.mic_volume = v;
}
void VoiceEngine::SetOutVolume(float v)  {
    std::lock_guard<std::mutex> lk(m_cfg_mtx);
    m_cfg.out_volume = v;
}
void VoiceEngine::SetBitrate(int bps) {
    if (m_encoder) opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(bps));
    std::lock_guard<std::mutex> lk(m_cfg_mtx);
    m_cfg.bitrate = bps;
}
void VoiceEngine::SetVoiceMode(VoiceMode m, int ptt_key) {
    std::lock_guard<std::mutex> lk(m_cfg_mtx);
    m_cfg.voice_mode = m;
    if (ptt_key) m_cfg.ptt_key = ptt_key;
}
void VoiceEngine::SetVadThreshold(float t) {
    std::lock_guard<std::mutex> lk(m_cfg_mtx);
    m_cfg.vad_threshold = t;
}
float VoiceEngine::GetInputLevel()  const { return m_in_level.load(); }
float VoiceEngine::GetOutputLevel() const { return m_out_level.load(); }

void VoiceEngine::StartMicTest() { m_mic_test.store(true); }
void VoiceEngine::StopMicTest()  { m_mic_test.store(false); }

// ─────────────────────────────────────────────────────────────────────────────
// Device enumeration
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<std::wstring> EnumDevices(EDataFlow flow) {
    std::vector<std::wstring> result;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                 __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator))))
        return result;

    IMMDeviceCollection* coll = nullptr;
    enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll);
    enumerator->Release();
    if (!coll) return result;

    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        coll->Item(i, &dev);
        if (!dev) continue;
        IPropertyStore* props = nullptr;
        dev->OpenPropertyStore(STGM_READ, &props);
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (props) {
            props->GetValue(PKEY_Device_FriendlyName, &pv);
            props->Release();
        }
        if (pv.vt == VT_LPWSTR)
            result.push_back(pv.pwszVal);
        PropVariantClear(&pv);
        dev->Release();
    }
    coll->Release();
    return result;
}

std::vector<std::wstring> VoiceEngine::EnumInputDevices()  { return EnumDevices(eCapture); }
std::vector<std::wstring> VoiceEngine::EnumOutputDevices() { return EnumDevices(eRender);  }
