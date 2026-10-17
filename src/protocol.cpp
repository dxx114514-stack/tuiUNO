#include "uno/protocol.hpp"

#include <cstring>

namespace uno {
namespace proto {

std::string encode(MsgType type, const std::string& payload) {
    ByteWriter w;
    w.u8(static_cast<uint8_t>(type));
    w.u32(static_cast<uint32_t>(payload.size()));
    std::string frame = w.take();
    frame.append(payload);
    return frame;
}

void FrameDecoder::feed(const std::string& bytes) { buf_.append(bytes); }

bool FrameDecoder::next(Message& out) {
    // Need at least a 5-byte header.
    if (buf_.size() < 5) return false;
    ByteReader r(buf_.data(), 5);
    out.type = static_cast<MsgType>(r.u8());
    const uint32_t len = r.u32();
    if (buf_.size() < 5 + static_cast<size_t>(len)) return false;
    out.payload = buf_.substr(5, len);
    buf_.erase(0, 5 + static_cast<size_t>(len));
    return true;
}

// ---------------------------------------------------------------------------
// payloads
// ---------------------------------------------------------------------------

std::string makeHello(const std::string& name, uint8_t lang) {
    ByteWriter w;
    w.u32(kProtocolVersion);
    w.str(name);
    w.u8(lang);
    return w.take();
}

bool parseHello(const std::string& p, std::string& name, uint8_t& lang) {
    ByteReader r(p);
    if (r.u32() != kProtocolVersion) return false;
    name = r.str();
    lang = r.u8();
    return r.ok();
}

std::string makeCreateRoom(const RoomConfig& cfg) {
    ByteWriter w;
    w.u32(kProtocolVersion);
    w.rules(cfg.rules);
    w.str(cfg.name);
    w.u8(cfg.lang);
    w.u32(cfg.seed);
    w.u8(cfg.fillWithBots ? 1 : 0);
    w.u8(static_cast<uint8_t>(cfg.botDifficulty));
    return w.take();
}

bool parseCreateRoom(const std::string& p, RoomConfig& cfg) {
    ByteReader r(p);
    if (r.u32() != kProtocolVersion) return false;
    cfg.rules = r.rules();
    cfg.name = r.str();
    cfg.lang = r.u8();
    cfg.seed = r.u32();
    cfg.fillWithBots = r.u8() != 0;
    cfg.botDifficulty = r.u8();
    return r.ok();
}

std::string makeJoinRoom(const std::string& code, const std::string& name, uint8_t lang) {
    ByteWriter w;
    w.u32(kProtocolVersion);
    w.str(code);
    w.str(name);
    w.u8(lang);
    return w.take();
}

bool parseJoinRoom(const std::string& p, std::string& code, std::string& name, uint8_t& lang) {
    ByteReader r(p);
    if (r.u32() != kProtocolVersion) return false;
    code = r.str();
    name = r.str();
    lang = r.u8();
    return r.ok();
}

std::string makeAction(const Action& a) {
    ByteWriter w;
    w.action(a);
    return w.take();
}

bool parseAction(const std::string& p, Action& a) {
    ByteReader r(p);
    a = r.action();
    return r.ok();
}

std::string makeChat(const std::string& from, const std::string& text) {
    ByteWriter w;
    w.str(from);
    w.str(text);
    return w.take();
}

bool parseChat(const std::string& p, std::string& from, std::string& text) {
    ByteReader r(p);
    from = r.str();
    text = r.str();
    return r.ok();
}

std::string makeWelcome(int seat, const std::string& name, const std::string& code, bool isHost) {
    ByteWriter w;
    w.u32(kProtocolVersion);
    w.u8(static_cast<uint8_t>(seat));
    w.str(name);
    w.str(code);
    w.u8(isHost ? 1 : 0);
    return w.take();
}

bool parseWelcome(const std::string& p, int& seat, std::string& name, std::string& code,
                  bool& isHost) {
    ByteReader r(p);
    if (r.u32() != kProtocolVersion) return false;
    seat = r.u8();
    name = r.str();
    code = r.str();
    isHost = r.u8() != 0;
    return r.ok();
}

std::string makeLobby(const LobbyState& s) {
    ByteWriter w;
    w.u16(static_cast<uint16_t>(s.seats.size()));
    for (const LobbySeat& seat : s.seats) {
        w.str(seat.name);
        w.u8(seat.ready ? 1 : 0);
        w.u8(seat.connected ? 1 : 0);
        w.u8(seat.isBot ? 1 : 0);
    }
    w.rules(s.rules);
    w.u8(static_cast<uint8_t>(s.hostSeat));
    w.u8(static_cast<uint8_t>(s.botDifficulty));
    w.u8(s.fillWithBots ? 1 : 0);
    w.u16(static_cast<uint16_t>(s.chat.size()));
    for (const std::string& c : s.chat) w.str(c);
    return w.take();
}

bool parseLobby(const std::string& p, LobbyState& s) {
    ByteReader r(p);
    const uint16_t n = r.u16();
    s.seats.clear();
    s.seats.reserve(n);
    for (uint16_t i = 0; i < n && r.ok(); ++i) {
        LobbySeat seat;
        seat.name = r.str();
        seat.ready = r.u8() != 0;
        seat.connected = r.u8() != 0;
        seat.isBot = r.u8() != 0;
        s.seats.push_back(seat);
    }
    s.rules = r.rules();
    s.hostSeat = r.u8();
    s.botDifficulty = r.u8();
    s.fillWithBots = r.u8() != 0;
    const uint16_t nc = r.u16();
    s.chat.clear();
    for (uint16_t i = 0; i < nc && r.ok(); ++i) s.chat.push_back(r.str());
    return r.ok();
}

std::string makeError(const std::string& code, const std::string& message) {
    ByteWriter w;
    w.str(code);
    w.str(message);
    return w.take();
}

bool parseError(const std::string& p, std::string& code, std::string& message) {
    ByteReader r(p);
    code = r.str();
    message = r.str();
    return r.ok();
}

std::string makeView(const View& v) {
    ByteWriter w;
    writeView(w, v);
    return w.take();
}

bool parseView(const std::string& p, View& v) {
    ByteReader r(p);
    return readView(r, v);
}

}  // namespace proto
}  // namespace uno