// Thin Winsock / BSD-socket portability layer shared by the device client and
// the fake-server tests. Only what those two need.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
namespace tt {
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline void net_init() {
    static const bool once = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)once;
}
constexpr int kSendFlags = 0;
inline int net_close(SocketHandle s) { return closesocket(s); }
inline int net_poll(SocketHandle s, short events, int timeout_ms) {
    WSAPOLLFD fd;
    fd.fd = s;
    fd.events = events;
    fd.revents = 0;
    int r = WSAPoll(&fd, 1, timeout_ms);
    return r > 0 ? fd.revents : r;
}
inline bool net_set_nonblocking(SocketHandle s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
inline int net_last_error() { return WSAGetLastError(); }
inline bool net_would_block(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline bool net_interrupted(int) { return false; }
}  // namespace tt
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
namespace tt {
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
inline void net_init() {}
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;  // never raise SIGPIPE on a closed peer
#else
constexpr int kSendFlags = 0;
#endif
inline int net_close(SocketHandle s) { return ::close(s); }
inline int net_poll(SocketHandle s, short events, int timeout_ms) {
    pollfd fd;
    fd.fd = s;
    fd.events = events;
    fd.revents = 0;
    int r = ::poll(&fd, 1, timeout_ms);
    return r > 0 ? fd.revents : r;
}
inline bool net_set_nonblocking(SocketHandle s) {
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
inline int net_last_error() { return errno; }
inline bool net_would_block(int e) { return e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN; }
inline bool net_interrupted(int e) { return e == EINTR; }
}  // namespace tt
#endif
