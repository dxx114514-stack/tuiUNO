// Thin cross-platform TCP wrapper. Winsock2 on Windows, BSD sockets elsewhere.
// All reads/writes are non-blocking; framing lives in protocol.hpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "uno/game.hpp"

namespace uno {
namespace net {

using Socket = intptr_t;
inline constexpr Socket kInvalidSocket = -1;
// A peer that stops reading must not make the host buffer forever.
inline constexpr size_t kDefaultOutBufLimit = 1u << 20;

// One-time process setup (WSAStartup) and console mode tweaks.
void init();

bool listenOn(Socket& out, const std::string& bindAddr, uint16_t port, int backlog = 8);
// Connects and stores the new socket in `out`. Blocking.
bool connectTo(const std::string& host, uint16_t port, Socket& out, std::string* err);
// Accepts one pending connection. The listener is non-blocking, so this returns
// false immediately when there is nothing waiting.
bool acceptOne(Socket listener, Socket& out);
void closeSocket(Socket s);

// Attempts one non-blocking write. Returns the number of bytes accepted, 0 when
// the socket would block, or -1 when the connection is broken.
long long sendSome(Socket s, const char* data, size_t len);
// Writes the whole buffer in one go, or returns false. A short write is a
// failure: callers must never treat a partially written frame as success.
bool sendRaw(Socket s, const char* data, size_t len);

// recvRaw appends whatever arrived and returns false only when the peer has
// gone away or the socket has failed.
bool recvRaw(Socket s, std::string& buf, size_t maxBytes = 8192);

// Per-peer outbound queue. Frames are always queued whole, so a would-block
// merely delays the remainder; it can never split a frame on the wire.
class OutBuf {
  public:
    explicit OutBuf(size_t limit = kDefaultOutBufLimit) : limit_(limit) {}

    // Queues a complete frame. Returns false when the peer has fallen too far
    // behind (or the frame itself is too large); close the connection then.
    bool push(const std::string& bytes);

    // Flushes as much as the socket accepts. Returns false on a fatal error;
    // the caller must close the connection rather than continue with a
    // half-written stream. A 0-byte write keeps the remainder queued.
    bool flush(Socket s);
    // Injectable writer, used by flush() and by the unit tests.
    bool flushWith(const std::function<long long(const char*, size_t)>& writer);

    bool empty() const { return buf_.empty(); }
    size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

  private:
    std::string buf_;
    size_t limit_;
};

void sleepMs(int ms);

// Local IPv4 address of this machine, empty when unavailable.
std::string localAddress();

}  // namespace net
}  // namespace uno
