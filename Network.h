// ─────────────────────────────────────────────────────────────────────────────
// Network.cpp  –  OnlyFrags Com
// OpenSSL-backed TLS/TCP client.
// ─────────────────────────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>

#include <cassert>
#include <chrono>
#include <sstream>

#include "Network.h"
#include "Protocol.h"

// Convenience: UTF-8 → wide
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), sz);
    return w;
}
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
NetworkClient::NetworkClient(HWND ui_hwnd) : m_hwnd(ui_hwnd) {
    // Initialise Winsock (idempotent)
    WSADATA wd{};
    WSAStartup(MAKEWORD(2, 2), &wd);

    // Initialise OpenSSL
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    m_send_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

NetworkClient::~NetworkClient() {
    Disconnect();
    if (m_send_event) { CloseHandle(m_send_event); m_send_event = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkClient::Connect(const std::string& host, int port, bool verify_cert) {
    if (m_running.load()) Disconnect();
    m_running.store(true);
    m_thread = std::thread(&NetworkClient::WorkerThread, this, host, port, verify_cert);
    return true;
}

void NetworkClient::Disconnect() {
    m_running.store(false);
    SetEvent(m_send_event);    // unblock any waiting send
    if (m_thread.joinable()) m_thread.join();
}

bool NetworkClient::IsConnected() const { return m_connected.load(); }

// ─────────────────────────────────────────────────────────────────────────────
// Worker thread
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::WorkerThread(const std::string& host, int port, bool verify_cert) {
    // ── Create SSL context ────────────────────────────────────────────────────
    m_ctx = SSL_CTX_new(TLS_client_method());
    if (!m_ctx) { PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0); return; }

    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);

    if (!verify_cert) {
        // Accept self-signed server certificate (development / LAN use)
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_CTX_set_default_verify_paths(m_ctx);
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER, nullptr);
    }

    // ── Resolve host ──────────────────────────────────────────────────────────
    addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0);
        return;
    }

    // ── Create and connect socket ─────────────────────────────────────────────
    m_sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (m_sock == INVALID_SOCKET) {
        freeaddrinfo(res);
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0);
        return;
    }

    // TCP keep-alive
    BOOL ka = TRUE;
    setsockopt(m_sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&ka), sizeof(ka));

    if (connect(m_sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
        closesocket(m_sock); m_sock = INVALID_SOCKET;
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0);
        return;
    }
    freeaddrinfo(res);

    // ── TLS handshake ─────────────────────────────────────────────────────────
    m_ssl = SSL_new(m_ctx);
    SSL_set_fd(m_ssl, static_cast<int>(m_sock));
    SSL_set_tlsext_host_name(m_ssl, host.c_str());

    if (SSL_connect(m_ssl) != 1) {
        SSL_free(m_ssl); m_ssl = nullptr;
        SSL_CTX_free(m_ctx); m_ctx = nullptr;
        closesocket(m_sock); m_sock = INVALID_SOCKET;
        PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0);
        return;
    }

    m_connected.store(true);
    PostMessage(m_hwnd, WM_APP_CONNECTED, 0, 0);

    // ── Set socket to non-blocking for select()-based I/O ─────────────────────
    u_long mode = 1;
    ioctlsocket(m_sock, FIONBIO, &mode);

    // ── Main I/O loop ─────────────────────────────────────────────────────────
    std::string recv_buf;
    recv_buf.reserve(4096);
    char tmp[4096];
    auto last_hb = std::chrono::steady_clock::now();

    while (m_running.load()) {
        // ── Send queued messages ──────────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(m_send_mutex);
            while (!m_send_queue.empty()) {
                const std::string& msg = m_send_queue.front();
                int sent = 0;
                while (sent < static_cast<int>(msg.size())) {
                    int n = SSL_write(m_ssl,
                                     msg.data() + sent,
                                     static_cast<int>(msg.size()) - sent);
                    if (n <= 0) goto disconnect;
                    sent += n;
                }
                m_send_queue.pop();
            }
        }

        // ── Send periodic heartbeat ───────────────────────────────────────────
        {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_hb).count() >= 30) {
                nlohmann::json hb;
                hb["type"] = "heartbeat";
                SendJson(hb);
                last_hb = now;
            }
        }

        // ── Read available data ───────────────────────────────────────────────
        {
            int n = SSL_read(m_ssl, tmp, sizeof(tmp));
            if (n > 0) {
                recv_buf.append(tmp, n);
                // Parse all complete newline-delimited JSON messages
                size_t pos;
                while ((pos = recv_buf.find('\n')) != std::string::npos) {
                    std::string line = recv_buf.substr(0, pos);
                    recv_buf.erase(0, pos + 1);
                    if (line.empty()) continue;
                    try {
                        auto j = nlohmann::json::parse(line);
                        HandleMessage(j);
                    } catch (...) {}
                }
            } else {
                int err = SSL_get_error(m_ssl, n);
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    goto disconnect;
                }
                // No data yet – yield CPU
                WaitForSingleObject(m_send_event, 5);
            }
        }
        continue;

    disconnect:
        break;
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    if (m_ssl) { SSL_shutdown(m_ssl); SSL_free(m_ssl); m_ssl = nullptr; }
    if (m_ctx) { SSL_CTX_free(m_ctx); m_ctx = nullptr; }
    if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    m_connected.store(false);
    PostMessage(m_hwnd, WM_APP_DISCONNECTED, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
bool NetworkClient::SendJson(const nlohmann::json& j) {
    std::string payload = j.dump() + "\n";
    std::lock_guard<std::mutex> lock(m_send_mutex);
    m_send_queue.push(payload);
    SetEvent(m_send_event);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Handle one inbound server message
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::HandleMessage(const nlohmann::json& j) {
    std::string type = j.value("type", "");

    if (type == "login_ok") {
        {
            std::lock_guard<std::mutex> lk(m_token_mutex);
            m_token = j.value("token", "");
        }
        PostLoginOk(Utf8ToWide(j.value("username", "")), j);
    }
    else if (type == "error") {
        std::string code = j.value("code", "");
        std::string msg  = j.value("message", "Error");
        if (code == "invalid_credentials")
            PostLoginFail(Utf8ToWide(msg));
        else if (code == "invalid_invite" || code == "username_taken" ||
                 code == "email_taken"    || code == "weak_password"   ||
                 code == "registration_failed")
            PostRegisterFail(Utf8ToWide(msg));
    }
    else if (type == "register_ok") {
        PostRegisterOk();
    }
    else if (type == "chat_message") {
        PostChatMessage(j);
    }
    else if (type == "voice_channel_state") {
        PostVoiceState(j);
    }
    else if (type == "voice_channel_count") {
        PostVoiceCount(j);
    }
    else if (type == "joined_voice") {
        auto* ch = new std::wstring(Utf8ToWide(j.value("channel", "")));
        PostMessage(m_hwnd, WM_APP_JOINED_VOICE, 0, reinterpret_cast<LPARAM>(ch));
    }
    else if (type == "left_voice") {
        PostMessage(m_hwnd, WM_APP_LEFT_VOICE, 0, 0);
    }
    else if (type == "udp_registered") {
        m_server_udp_port = j.value("server_udp_port", 5001);
        PostMessage(m_hwnd, WM_APP_UDP_READY, 0, 0);
    }
    else if (type == "heartbeat_ack") {
        // nothing – just confirming server is alive
    }
    // history, voice_channel_count etc. are handled analogously
}

// ─────────────────────────────────────────────────────────────────────────────
// Post helpers – heap-allocate the payload; UI thread must delete.
// ─────────────────────────────────────────────────────────────────────────────
void NetworkClient::PostLoginOk(const std::wstring& username, const nlohmann::json& j) {
    // Package channel lists alongside the username
    // We store as a simple struct wrapper
    auto* p = new std::wstring(username);
    // Also forward the full json for channel enumeration via a second message
    PostMessage(m_hwnd, WM_APP_LOGIN_OK, 0, reinterpret_cast<LPARAM>(p));
    // Post channel info separately via generic net message
    auto* jcopy = new nlohmann::json(j);
    PostMessage(m_hwnd, WM_APP_NET_MSG, 0, reinterpret_cast<LPARAM>(jcopy));
}

void NetworkClient::PostLoginFail(const std::wstring& msg) {
    auto* p = new std::wstring(msg);
    PostMessage(m_hwnd, WM_APP_LOGIN_FAIL, 0, reinterpret_cast<LPARAM>(p));
}

void NetworkClient::PostRegisterOk() {
    PostMessage(m_hwnd, WM_APP_REGISTER_OK, 0, 0);
}

void NetworkClient::PostRegisterFail(const std::wstring& msg) {
    auto* p = new std::wstring(msg);
    PostMessage(m_hwnd, WM_APP_REGISTER_FAIL, 0, reinterpret_cast<LPARAM>(p));
}

void NetworkClient::PostChatMessage(const nlohmann::json& j) {
    auto* d = new ChatMessageData();
    d->channel   = Utf8ToWide(j.value("channel",   ""));
    d->username  = Utf8ToWide(j.value("username",  ""));
    d->content   = Utf8ToWide(j.value("content",   ""));
    d->timestamp = Utf8ToWide(j.value("timestamp", ""));
    PostMessage(m_hwnd, WM_APP_CHAT_MSG, 0, reinterpret_cast<LPARAM>(d));
}

void NetworkClient::PostVoiceState(const nlohmann::json& j) {
    auto* d = new VoiceStateData();
    d->channel = Utf8ToWide(j.value("channel", ""));
    if (j.contains("users") && j["users"].is_array()) {
        for (const auto& u : j["users"]) {
            VoiceUser vu;
            vu.username  = Utf8ToWide(u.value("username",  ""));
            vu.muted     = u.value("muted",    false);
            vu.deafened  = u.value("deafened", false);
            vu.speaking  = u.value("speaking", false);
            d->users.push_back(vu);
        }
    }
    PostMessage(m_hwnd, WM_APP_VOICE_STATE, 0, reinterpret_cast<LPARAM>(d));
}

void NetworkClient::PostVoiceCount(const nlohmann::json& j) {
    auto* d = new VoiceCountData();
    d->channel = Utf8ToWide(j.value("channel", ""));
    d->count   = j.value("count", 0);
    if (j.contains("users") && j["users"].is_array())
        for (const auto& u : j["users"])
            d->usernames.push_back(Utf8ToWide(u.get<std::string>()));
    PostMessage(m_hwnd, WM_APP_VOICE_COUNT, 0, reinterpret_cast<LPARAM>(d));
}

// ─────────────────────────────────────────────────────────────────────────────
// Public send methods
// ─────────────────────────────────────────────────────────────────────────────
std::string NetworkClient::GetToken() const {
    std::lock_guard<std::mutex> lk(m_token_mutex);
    return m_token;
}

int NetworkClient::GetServerUdpPort() const { return m_server_udp_port; }

void NetworkClient::SendLogin(const std::string& email, const std::string& pw) {
    nlohmann::json j;
    j["type"]     = "login";
    j["email"]    = email;
    j["password"] = pw;
    SendJson(j);
}

void NetworkClient::SendRegister(const std::string& uname, const std::string& email,
                                 const std::string& pw,    const std::string& code) {
    nlohmann::json j;
    j["type"]        = "register";
    j["username"]    = uname;
    j["email"]       = email;
    j["password"]    = pw;
    j["invite_code"] = code;
    SendJson(j);
}

void NetworkClient::JoinVoiceChannel(const std::string& channel) {
    nlohmann::json j;
    j["type"]    = "join_voice";
    j["channel"] = channel;
    SendJson(j);
}

void NetworkClient::LeaveVoiceChannel() {
    nlohmann::json j;
    j["type"] = "leave_voice";
    SendJson(j);
}

void NetworkClient::SetMuted(bool muted) {
    nlohmann::json j;
    j["type"]  = "mute";
    j["muted"] = muted;
    SendJson(j);
}

void NetworkClient::SetDeafened(bool deafened) {
    nlohmann::json j;
    j["type"]     = "deafen";
    j["deafened"] = deafened;
    SendJson(j);
}

void NetworkClient::SendChat(const std::string& channel, const std::string& content) {
    nlohmann::json j;
    j["type"]    = "chat";
    j["channel"] = channel;
    j["content"] = content;
    SendJson(j);
}

void NetworkClient::GetHistory(const std::string& channel, int limit) {
    nlohmann::json j;
    j["type"]    = "get_history";
    j["channel"] = channel;
    j["limit"]   = limit;
    SendJson(j);
}

void NetworkClient::RegisterUdpPort(int local_udp_port) {
    nlohmann::json j;
    j["type"]     = "register_udp";
    j["udp_port"] = local_udp_port;
    SendJson(j);
}

void NetworkClient::SendHeartbeat() {
    nlohmann::json j;
    j["type"] = "heartbeat";
    SendJson(j);
}
