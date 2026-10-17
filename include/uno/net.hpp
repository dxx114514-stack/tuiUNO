// Thin cross-platform TCP wrapper. Winsock2 on Windows, BSD sockets elsewhere.
// All reads/writes are non-blocking; framing lives in protocol.hpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uno/game.hpp"

namespace uno {
namespace net {

using Socket = intptr_t;
inline constexpr Socket kInvalidSocket = -1;

// One-time process setup (WSAStartup) and console mode tweaks.
void init();

bool listenOn(Socket& out, const std::string& bindAddr, uint16_t port, int backlog = 8);
// Connects and stores the new socket in `out`. Blocking.
bool connectTo(const std::string& host, uint16_t port, Socket& out, std::string* err);
// Accepts one pending connection. The listener is non-blocking, so this returns
// false immediately when there is nothing waiting.
bool acceptOne(Socket listener, Socket& out);
void closeSocket(Socket s);

// All sockets are non-blocking, so these never block:
//   sendRaw returns false when the write could not be completed;
//   recvRaw appends whatever arrived and returns false only when the peer has
//   gone away or the socket has failed.
bool sendRaw(Socket s, const char* data, size_t len);
bool recvRaw(Socket s, std::string& buf, size_t maxBytes = 8192);

void sleepMs(int ms);

// Local IPv4 address of this machine, empty when unavailable.
std::string localAddress();

}  // namespace net
}  // namespace uno