// Tiny, bounded HTTP/1.1 listener that turns authenticated webhook calls into
// TomTom notifications.
//
//   POST /notify            Authorization: Bearer <token>   (or  X-Webhook-Token: <token>)
//     Content-Type: application/json   {"text": "Build finished", "ttl": 15}
//        (also accepts "message" or "content" for the text, and "seconds" for the ttl,
//         so a Discord-style {"content": "..."} payload works unchanged)
//     any other Content-Type            body is the message text; optional ?ttl=15
//   GET /health             200 "ok" (no authentication, no side effects)
//
// Security model: loopback-only by default; "allow LAN" additionally accepts peers
// from private (RFC 1918 / link-local) addresses. A random token is always required,
// requests are capped at 4 KiB of headers + 1 KiB of body with a 2 s total read
// deadline, and notifications are rate limited to two per second. Traffic is plain
// HTTP, so do not publish the port beyond a network you trust.
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace tt {

constexpr int kDefaultWebhookPort = 18750;
constexpr size_t kMinWebhookTokenLength = 16;

class WebhookServer {
public:
    struct Config {
        int port = kDefaultWebhookPort;
        bool allow_lan = false;
        std::string token;
    };
    // Called on the server thread for each accepted request; return false (and set
    // `error`) to answer 502 instead of 200.
    using Handler = std::function<bool(int ttl_seconds, const std::string& text, std::string& error)>;

    struct Status {
        unsigned long delivered = 0;
        unsigned long rejected = 0;
        std::string last;  // short, printable-ASCII description of the latest event
    };

    WebhookServer() = default;
    ~WebhookServer();
    WebhookServer(const WebhookServer&) = delete;
    WebhookServer& operator=(const WebhookServer&) = delete;

    bool start(const Config& config, Handler handler, std::string& error);
    void stop();
    bool running() const { return running_; }
    Status status() const;

private:
    void run();
    void handle_client(unsigned long long socket_handle);
    void note(bool delivered, const std::string& what);

    Config config_;
    Handler handler_;
    unsigned long long listener_ = ~0ULL;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    Status status_;
    long long last_accept_ms_ = -1000000;
};

// Cryptographically random lowercase-hex token (default 32 chars = 128 bits).
std::string generate_webhook_token(size_t bytes = 16);

}  // namespace tt
