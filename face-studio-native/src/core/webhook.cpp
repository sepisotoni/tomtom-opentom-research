#include "webhook.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#include "json.h"
#include "net_compat.h"
#include "notify.h"

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <cstdio>
#endif

namespace tt {

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kMaxHeaderBytes = 4096;
constexpr size_t kMaxBodyBytes = 1024;
constexpr int kReadDeadlineMs = 2000;
constexpr int kMinGapMs = 500;
constexpr int kDefaultTtl = 15;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

SocketHandle as_socket(unsigned long long v) { return static_cast<SocketHandle>(v); }

bool send_all(SocketHandle s, const std::string& data) {
    size_t sent = 0;
    auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    while (sent < data.size()) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0 || net_poll(s, POLLOUT, static_cast<int>(left)) <= 0) return false;
        int n = static_cast<int>(::send(s, data.data() + sent, static_cast<int>(data.size() - sent), kSendFlags));
        if (n <= 0) {
            int e = net_last_error();
            if (n < 0 && (net_would_block(e) || net_interrupted(e))) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void respond(SocketHandle s, int code, const char* reason, const std::string& body) {
    std::string r = "HTTP/1.1 " + std::to_string(code) + " " + reason +
                    "\r\nContent-Type: text/plain; charset=us-ascii\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n" + body;
    send_all(s, r);
}

bool constant_time_equals(const std::string& a, const std::string& b) {
    size_t n = std::max(a.size(), b.size());
    unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        diff |= static_cast<unsigned char>(x ^ y);
    }
    return diff == 0;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

bool peer_allowed(const sockaddr_in& peer, bool allow_lan) {
    unsigned long a = ntohl(peer.sin_addr.s_addr);
    unsigned b0 = (a >> 24) & 0xFF, b1 = (a >> 16) & 0xFF;
    if (b0 == 127) return true;
    if (!allow_lan) return false;
    return b0 == 10 || (b0 == 172 && b1 >= 16 && b1 <= 31) || (b0 == 192 && b1 == 168) || (b0 == 169 && b1 == 254);
}

int percent_decode_int(const std::string& s, int fallback) {
    if (s.empty() || s.size() > 4) return fallback;
    int v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return fallback;
        v = v * 10 + (c - '0');
    }
    return v;
}

std::string query_param(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key) return pair.substr(eq + 1);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}

}  // namespace

WebhookServer::~WebhookServer() { stop(); }

bool WebhookServer::start(const Config& config, Handler handler, std::string& error) {
    if (running_) { error = "The webhook is already running."; return false; }
    if (config.token.size() < kMinWebhookTokenLength) {
        error = "The webhook token must be at least 16 characters.";
        return false;
    }
    if (config.port < 1024 || config.port > 65535) {
        error = "Choose a webhook port between 1024 and 65535.";
        return false;
    }
    net_init();
    SocketHandle s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) { error = "Could not create a network socket."; return false; }
#ifdef _WIN32
    // Refuse to share the port with another process (SO_REUSEADDR would allow hijacking on Windows).
    int excl = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof excl);
#else
    int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
#endif
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(config.port));
    addr.sin_addr.s_addr = htonl(config.allow_lan ? INADDR_ANY : INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 4) != 0 || !net_set_nonblocking(s)) {
        net_close(s);
        error = "Could not listen on port " + std::to_string(config.port) + " (is it already in use?).";
        return false;
    }
    config_ = config;
    handler_ = std::move(handler);
    listener_ = static_cast<unsigned long long>(s);
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = Status();
        status_.last = "listening";
    }
    last_accept_ms_ = now_ms() - 1000000;
    running_ = true;
    thread_ = std::thread([this] { run(); });
    return true;
}

void WebhookServer::stop() {
    if (!running_) return;
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    net_close(as_socket(listener_));
    listener_ = ~0ULL;
    running_ = false;
}

WebhookServer::Status WebhookServer::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void WebhookServer::note(bool delivered, const std::string& what) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (delivered) ++status_.delivered; else ++status_.rejected;
    status_.last = to_printable_ascii(what, 60);
}

void WebhookServer::run() {
    SocketHandle listener = as_socket(listener_);
    while (!stop_) {
        int ev = net_poll(listener, POLLIN, 200);
        if (ev <= 0) continue;
        sockaddr_in peer;
        std::memset(&peer, 0, sizeof peer);
#ifdef _WIN32
        int len = sizeof peer;
#else
        socklen_t len = sizeof peer;
#endif
        SocketHandle c = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &len);
        if (c == kInvalidSocket) continue;
        if (!peer_allowed(peer, config_.allow_lan)) {
            note(false, "refused a peer outside the allowed networks");
            net_close(c);
            continue;
        }
        net_set_nonblocking(c);
        handle_client(static_cast<unsigned long long>(c));
        net_close(c);
    }
}

void WebhookServer::handle_client(unsigned long long handle) {
    SocketHandle s = as_socket(handle);
    const auto deadline = Clock::now() + std::chrono::milliseconds(kReadDeadlineMs);
    std::string data;
    size_t header_end = std::string::npos;
    char buf[1024];

    auto read_more = [&]() -> int {  // 1 = got data, 0 = closed/error/timeout
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) return 0;
        int ev = net_poll(s, POLLIN, static_cast<int>(left));
        if (ev <= 0) return 0;
        int n = static_cast<int>(::recv(s, buf, sizeof buf, 0));
        if (n <= 0) {
            if (n < 0 && (net_would_block(net_last_error()) || net_interrupted(net_last_error()))) return 1;
            return 0;
        }
        data.append(buf, static_cast<size_t>(n));
        return 1;
    };

    while (header_end == std::string::npos) {
        header_end = data.find("\r\n\r\n");
        if (header_end != std::string::npos) break;
        if (data.size() > kMaxHeaderBytes) { respond(s, 431, "Request Header Fields Too Large", "headers too large\n"); note(false, "oversized headers"); return; }
        if (!read_more()) { note(false, "incomplete request"); return; }
    }

    // Request line + headers
    std::string head = data.substr(0, header_end);
    size_t line_end = head.find("\r\n");
    std::string request_line = head.substr(0, line_end);
    size_t sp1 = request_line.find(' '), sp2 = request_line.rfind(' ');
    if (sp1 == std::string::npos || sp2 == sp1) { respond(s, 400, "Bad Request", "bad request\n"); note(false, "malformed request line"); return; }
    std::string method = request_line.substr(0, sp1);
    std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string version = request_line.substr(sp2 + 1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") { respond(s, 400, "Bad Request", "bad request\n"); note(false, "unsupported HTTP version"); return; }
    std::string path = target, query;
    size_t qm = target.find('?');
    if (qm != std::string::npos) { path = target.substr(0, qm); query = target.substr(qm + 1); }

    std::string content_type, bearer, header_token;
    long long content_length = 0;
    bool has_length = false;
    size_t p = line_end == std::string::npos ? head.size() : line_end + 2;
    while (p < head.size()) {
        size_t e = head.find("\r\n", p);
        std::string line = head.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? head.size() : e + 2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = lower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (name == "content-length") {
            if (value.empty() || value.size() > 9 || value.find_first_not_of("0123456789") != std::string::npos) {
                respond(s, 400, "Bad Request", "bad content-length\n"); note(false, "bad content-length"); return;
            }
            content_length = std::stoll(value);
            has_length = true;
        } else if (name == "content-type") content_type = lower(value);
        else if (name == "authorization") {
            if (lower(value).rfind("bearer ", 0) == 0) bearer = trim(value.substr(7));
        } else if (name == "x-webhook-token") header_token = value;
        else if (name == "transfer-encoding") { respond(s, 501, "Not Implemented", "chunked bodies are not supported\n"); note(false, "chunked body"); return; }
    }

    if (method == "GET" && path == "/health") { respond(s, 200, "OK", "ok\n"); return; }
    if (path != "/notify" && path != "/notify/") { respond(s, 404, "Not Found", "not found\n"); note(false, "unknown path"); return; }
    if (method != "POST") { respond(s, 405, "Method Not Allowed", "use POST\n"); note(false, "wrong method"); return; }

    const std::string& presented = !bearer.empty() ? bearer : header_token;
    if (presented.empty() || !constant_time_equals(presented, config_.token)) {
        respond(s, 401, "Unauthorized", "missing or wrong token\n");
        note(false, "rejected: bad token");
        return;
    }
    if (!has_length || content_length < 1) { respond(s, 411, "Length Required", "a request body is required\n"); note(false, "no body"); return; }
    if (content_length > static_cast<long long>(kMaxBodyBytes)) { respond(s, 413, "Payload Too Large", "body too large\n"); note(false, "oversized body"); return; }

    size_t body_start = header_end + 4;
    while (data.size() < body_start + static_cast<size_t>(content_length)) {
        if (!read_more()) { note(false, "incomplete body"); return; }
    }
    std::string body = data.substr(body_start, static_cast<size_t>(content_length));

    int ttl = percent_decode_int(query_param(query, "ttl"), kDefaultTtl);
    std::string text;
    if (content_type.find("json") != std::string::npos) {
        Json j;
        std::string perr;
        if (!Json::parse(body, j, perr)) { respond(s, 400, "Bad Request", "invalid JSON\n"); note(false, "invalid JSON"); return; }
        if (j.is_string()) {
            text = j.as_string();
        } else if (j.is_object()) {
            for (const char* key : {"text", "message", "content"}) {
                const Json& v = j.get(key);
                if (v.is_string()) { text = v.as_string(); break; }
            }
            for (const char* key : {"ttl", "seconds"}) {
                const Json& v = j.get(key);
                if (v.is_int() && v.as_int() >= 0 && v.as_int() <= 100000) { ttl = static_cast<int>(v.as_int()); break; }
            }
        }
    } else {
        text = body;
    }
    if (!has_displayable_text(sanitize_notification_text(text))) { respond(s, 400, "Bad Request", "no displayable text\n"); note(false, "no displayable text"); return; }

    long long t = now_ms();
    if (t - last_accept_ms_ < kMinGapMs) { respond(s, 429, "Too Many Requests", "slow down\n"); note(false, "rate limited"); return; }
    last_accept_ms_ = t;

    std::string herr;
    if (handler_ && handler_(ttl, text, herr)) {
        respond(s, 200, "OK", "ok\n");
        note(true, "delivered: " + sanitize_notification_text(text));
    } else {
        respond(s, 502, "Bad Gateway", "could not deliver to the TomTom\n");
        note(false, "delivery failed: " + herr);
    }
}

std::string generate_webhook_token(size_t bytes) {
    std::vector<unsigned char> raw(bytes);
    bool ok = false;
#ifdef _WIN32
    ok = BCryptGenRandom(nullptr, raw.data(), static_cast<ULONG>(raw.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    if (FILE* f = std::fopen("/dev/urandom", "rb")) {
        ok = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
        std::fclose(f);
    }
#endif
    if (!ok) return "";  // never fall back to a weak generator
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : raw) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 15]); }
    return out;
}

}  // namespace tt
