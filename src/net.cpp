#include "uno/net.hpp"

#include <climits>
#include <cstring>

#if UNO_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace uno {
namespace net {

namespace {

#if UNO_WIN
using RawSocket = SOCKET;
using SockLen = int;
constexpr RawSocket kBad = INVALID_SOCKET;
#else
using RawSocket = int;
using SockLen = ::socklen_t;
constexpr RawSocket kBad = -1;
#endif

void setNonBlocking(RawSocket s) {
#if UNO_WIN
    u_long mode = 1;
    ::ioctlsocket(s, FIONBIO, &mode);
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

void shutdownBoth(RawSocket s) {
#if UNO_WIN
    ::shutdown(s, SD_BOTH);
#else
    ::shutdown(s, SHUT_RDWR);
#endif
}

}  // namespace

void init() {
#if UNO_WIN
    static bool done = false;
    if (done) return;
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
    done = true;
#endif
}

bool listenOn(Socket& out, const std::string& bindAddr, uint16_t port, int backlog) {
    init();

    RawSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kBad) return false;

    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bindAddr.empty() || bindAddr == "0.0.0.0" || bindAddr == "*") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bindAddr.c_str(), &addr.sin_addr) != 1) {
        ::closesocket(s);
        return false;
    }

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(s);
        return false;
    }
    if (::listen(s, backlog) != 0) {
        ::closesocket(s);
        return false;
    }
    setNonBlocking(s);
    out = static_cast<Socket>(s);
    return true;
}

bool acceptOne(Socket listener, Socket& out) {
    if (listener == kInvalidSocket) return false;
    RawSocket s = ::accept(static_cast<RawSocket>(listener), nullptr, nullptr);
    if (s == kBad) return false;
    setNonBlocking(s);
    int yes = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    out = static_cast<Socket>(s);
    return true;
}

bool connectTo(const std::string& host, uint16_t port, Socket& out, std::string* err) {
    init();
    out = kInvalidSocket;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "cannot resolve host";
        return false;
    }

    RawSocket s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kBad) {
        ::freeaddrinfo(res);
        if (err) *err = "cannot create socket";
        return false;
    }

    const int rc = ::connect(s, res->ai_addr, static_cast<SockLen>(res->ai_addrlen));
    ::freeaddrinfo(res);
    if (rc != 0) {
        ::closesocket(s);
        if (err) *err = "connection refused or host unreachable";
        return false;
    }

    int yes = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setNonBlocking(s);
    out = static_cast<Socket>(s);
    return true;
}

void closeSocket(Socket sock) {
    if (sock == kInvalidSocket) return;
    shutdownBoth(static_cast<RawSocket>(sock));
#if UNO_WIN
    ::closesocket(static_cast<RawSocket>(sock));
#else
    ::close(static_cast<int>(sock));
#endif
}

long long sendSome(Socket sock, const char* data, size_t len) {
    if (sock == kInvalidSocket) return -1;
    if (len == 0) return 0;
    RawSocket s = static_cast<RawSocket>(sock);
    const size_t chunk = len > static_cast<size_t>(INT_MAX) ? static_cast<size_t>(INT_MAX) : len;
    for (;;) {
        const int n = ::send(s, data, static_cast<int>(chunk), 0);
        if (n > 0) return static_cast<long long>(n);
#if UNO_WIN
        const int e = ::WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 0;
        if (e == WSAEINTR) continue;
        return -1;
#else
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
#endif
    }
}

bool sendRaw(Socket sock, const char* data, size_t len) {
    if (sock == kInvalidSocket) return false;
    size_t sent = 0;
    while (sent < len) {
        const long long n = sendSome(sock, data + sent, len - sent);
        if (n <= 0) return false;  // would-block or broken: never claim success
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool OutBuf::push(const std::string& bytes) {
    if (bytes.size() > limit_ - buf_.size()) return false;
    buf_ += bytes;
    return true;
}

bool OutBuf::flush(Socket sock) {
    return flushWith([sock](const char* data, size_t len) {
        return sendSome(sock, data, len);
    });
}

bool OutBuf::flushWith(const std::function<long long(const char*, size_t)>& writer) {
    while (!buf_.empty()) {
        const long long n = writer(buf_.data(), buf_.size());
        if (n < 0) {
            buf_.clear();  // the stream is poisoned; the caller must close
            return false;
        }
        if (n == 0) return true;  // socket buffer full, retry later
        if (static_cast<size_t>(n) >= buf_.size()) {
            buf_.clear();
            return true;
        }
        buf_.erase(0, static_cast<size_t>(n));
    }
    return true;
}

bool recvRaw(Socket sock, std::string& buf, size_t maxBytes) {
    if (sock == kInvalidSocket) return false;
    RawSocket s = static_cast<RawSocket>(sock);
    char tmp[4096];
    const size_t want = maxBytes < sizeof(tmp) ? maxBytes : sizeof(tmp);
    const int n = ::recv(s, tmp, static_cast<int>(want), 0);
#if UNO_WIN
    const int e = ::WSAGetLastError();
#endif
    if (n > 0) {
        buf.append(tmp, static_cast<size_t>(n));
        return true;
    }
    if (n == 0) return false;  // orderly shutdown by peer
#if UNO_WIN
    if (e == WSAEWOULDBLOCK) return true;
    return false;
#else
    if (errno == EINTR) return true;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
    return false;
#endif
}

void sleepMs(int ms) {
    if (ms <= 0) return;
#if UNO_WIN
    ::Sleep(static_cast<DWORD>(ms));
#else
    ::usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

std::string localAddress() {
    char host[256];
    host[255] = '\0';
    if (::gethostname(host, sizeof(host) - 1) != 0) return {};

    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return {};

    char out[64] = {0};
    const auto* a = reinterpret_cast<const sockaddr_in*>(res->ai_addr);
    ::inet_ntop(AF_INET, &a->sin_addr, out, sizeof(out));
    ::freeaddrinfo(res);
    return out;
}

}  // namespace net
}  // namespace uno