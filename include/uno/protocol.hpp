// Message framing and payload helpers. Frame = [u8 type][u32 length][payload].
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uno/game.hpp"
#include "uno/serialize.hpp"

namespace uno {
namespace proto {

inline constexpr uint32_t kProtocolVersion = 1;
// A frame longer than this is treated as a protocol failure, not as "wait for
// more bytes": otherwise a desynchronised stream can make us buffer 4 GB.
inline constexpr uint32_t kMaxFramePayload = 1u << 20;
inline constexpr size_t kMaxNameCodepoints = 16;
inline constexpr size_t kMaxChatBytes = 200;

enum class MsgType : uint8_t {
    // client -> host
    Hello = 1,       // name, lang
    CreateRoom = 2,  // rules + host options
    JoinRoom = 3,    // room code, name, lang
    StartGame = 4,   // host only
    Action = 5,      // game action
    Chat = 6,
    Leave = 7,
    // host -> client
    Welcome = 80,    // seat, name, room code, you-are-host
    Rejected = 81,   // machine-readable reason
    Lobby = 82,      // seats, ready flags, rules
    StartGameAck = 83,
    View = 84,       // state snapshot
    ChatMsg = 85,
    Error = 86,
    MatchEnd = 87,
};

struct Message {
    MsgType type = MsgType::Hello;
    std::string payload;
};

std::string encode(MsgType type, const std::string& payload);

// Accumulates raw bytes and hands back whole frames.
class FrameDecoder {
  public:
    void feed(const std::string& bytes);
    bool next(Message& out);
    // True after next() saw an impossible frame length. The caller must close
    // the connection; there is no resynchronisation after a bad header.
    bool failed() const { return failed_; }

  private:
    std::string buf_;
    bool failed_ = false;
};

// --- payload builders / parsers --------------------------------------------

std::string makeHello(const std::string& name, uint8_t lang);
bool parseHello(const std::string& p, std::string& name, uint8_t& lang);

struct RoomConfig {
    Rules rules;
    std::string name;
    uint8_t lang = 0;
    uint32_t seed = 0;
    bool fillWithBots = true;
    int botDifficulty = 1;
};

std::string makeCreateRoom(const RoomConfig& cfg);
bool parseCreateRoom(const std::string& p, RoomConfig& cfg);

std::string makeJoinRoom(const std::string& code, const std::string& name, uint8_t lang);
bool parseJoinRoom(const std::string& p, std::string& code, std::string& name, uint8_t& lang);

enum class JoinCheck {
    Ok,
    BadRoom,     // supplied room code does not match the host's
    InProgress,  // the match has already started; seats are fixed
};
// Validates a join request. Keeping this pure makes the host's room-code gate
// directly testable (the previous inline comparison shadowed Room::code and
// therefore accepted every request).
JoinCheck checkJoin(const std::string& expectedCode, const std::string& suppliedCode,
                    bool started);

// Removes C0 controls (including ESC, CR and LF) and DEL from untrusted text,
// truncating to at most `maxCodepoints` UTF-8 code points. Malformed UTF-8 is
// dropped rather than copied into the terminal.
std::string sanitizeText(const std::string& in, size_t maxCodepoints);
// Truncates to at most `maxBytes` bytes without splitting a UTF-8 code point.
std::string truncateUtf8(const std::string& s, size_t maxBytes);

std::string makeAction(const Action& a);
bool parseAction(const std::string& p, Action& a);

std::string makeChat(const std::string& from, const std::string& text);
bool parseChat(const std::string& p, std::string& from, std::string& text);

std::string makeWelcome(int seat, const std::string& name, const std::string& code, bool isHost);
bool parseWelcome(const std::string& p, int& seat, std::string& name, std::string& code, bool& isHost);

struct LobbySeat {
    std::string name;
    bool ready = false;
    bool connected = true;
    bool isBot = false;
};

struct LobbyState {
    std::vector<LobbySeat> seats;
    Rules rules;
    int hostSeat = 0;
    int botDifficulty = 1;
    bool fillWithBots = true;
    std::vector<std::string> chat;
};

std::string makeLobby(const LobbyState& s);
bool parseLobby(const std::string& p, LobbyState& s);

std::string makeError(const std::string& code, const std::string& message);
bool parseError(const std::string& p, std::string& code, std::string& message);

std::string makeView(const View& v);
bool parseView(const std::string& p, View& v);

}  // namespace proto
}  // namespace uno