// Online PVP. The host owns the authoritative Game; remote clients submit
// actions over TCP. Everything is single-threaded: sockets are non-blocking and
// driven by explicit pump() calls from the match loop.
#include "uno/app.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <random>
#include <vector>

#include "uno/ai.hpp"
#include "uno/console.hpp"
#include "uno/net.hpp"
#include "uno/protocol.hpp"
#include "uno/session.hpp"
#include "uno/ui.hpp"

namespace uno {

namespace {

constexpr uint16_t kDefaultPort = 45231;
// How long an unattended host (no console on stdin) waits for remote players.
constexpr int kUnattendedGraceMs = 12000;
// A socket that has not completed a JoinRoom by then gives its seat back.
constexpr int kJoinTimeoutMs = 5000;

std::string cleanName(const std::string& raw) {
    return proto::sanitizeText(raw, proto::kMaxNameCodepoints);
}

std::string cleanChat(const std::string& raw) {
    return proto::truncateUtf8(
        proto::sanitizeText(raw, proto::kMaxChatBytes), proto::kMaxChatBytes);
}

const char* kR = "\x1b[0m";
const char* kBold = "\x1b[1m";
const char* kDim = "\x1b[2m";
const char* kGreen = "\x1b[32m";
const char* kYellow = "\x1b[33m";
const char* kCyan = "\x1b[36m";

uint8_t langCode(Lang l) { return l == Lang::Zh ? 1 : 0; }

std::string makeCode(std::mt19937& rng) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::uniform_int_distribution<int> d(0, 31);
    std::string code;
    for (int i = 0; i < 4; ++i) code += alphabet[d(rng)];
    return code;
}

std::string askText(const std::string& prompt, const std::string& fallback) {
    const std::string line = readLine(prompt);
    return line.empty() ? fallback : line;
}

uint16_t askPort() {
    for (;;) {
        const std::string s = askText("Port [" + S(kDefaultPort) + "]: ", S(kDefaultPort));
        try {
            const int p = std::stoi(s);
            if (p > 0 && p < 65536) return static_cast<uint16_t>(p);
        } catch (...) {
        }
        println(tr("common.invalid"));
    }
}

// ===========================================================================
// Host
// ===========================================================================

struct Peer {
    net::Socket sock = net::kInvalidSocket;
    int seat = -1;
    std::string name;
    std::string inBuf;
    net::OutBuf outBuf;
    proto::FrameDecoder decoder;
    std::deque<Action> actions;
    std::vector<std::string> chat;
    uint8_t lang = 0;
    bool joined = false;
    std::chrono::steady_clock::time_point connectedAt{};
};

struct Room {
    AppConfig cfg;
    Rules rules;
    std::string code;
    uint16_t port = 0;
    net::Socket listener = net::kInvalidSocket;
    std::mt19937 rng;
    std::vector<Peer> peers;          // index == seat; peers[0] is the host
    std::vector<std::string> names;   // display names, index == seat
    std::vector<bool> bot;            // seat is CPU-controlled
    std::vector<std::string> lobbyChat;
    GameUI* ui = nullptr;
    bool started = false;

    void initSeats() {
        peers.assign(static_cast<size_t>(rules.playerCount), Peer());
        names.assign(static_cast<size_t>(rules.playerCount), "");
        bot.assign(static_cast<size_t>(rules.playerCount), false);
        for (int i = 0; i < rules.playerCount; ++i) {
            peers[(size_t)i].seat = i;
            names[(size_t)i] = "CPU " + S(i + 1);
            bot[(size_t)i] = true;
        }
        bot[0] = false;                 // the host always plays
        names[0] = cfg.rules.playerCount ? std::string("host") : std::string("host");
        // Pre-fill every bot seat but the first with CPUs so a solo host can
        // start immediately; remote players may still take them over.
        for (int i = 2; i < rules.playerCount; ++i) bot[(size_t)i] = true;
    }

    bool openPort(uint16_t p) {
        if (!net::listenOn(listener, "", p)) return false;
        port = p;
        return true;
    }

    // -- io ---------------------------------------------------------------
    // Every frame is queued whole and flushed opportunistically. A would-block
    // leaves the remainder queued for the next pump(), so the stream can never
    // contain a half frame; a fatal error closes the peer (fail-closed).
    void flushPeer(int seat) {
        if (seat < 0 || seat >= static_cast<int>(peers.size())) return;
        Peer& p = peers[(size_t)seat];
        if (p.sock == net::kInvalidSocket) return;
        if (!p.outBuf.flush(p.sock)) closePeer(seat);
    }

    void send(int seat, proto::MsgType t, const std::string& payload) {
        if (seat < 0 || seat >= static_cast<int>(peers.size())) return;
        Peer& p = peers[(size_t)seat];
        if (p.sock == net::kInvalidSocket) return;
        const std::string frame = proto::encode(t, payload);
        if (!p.outBuf.push(frame)) {
            closePeer(seat);
            return;
        }
        flushPeer(seat);
    }

    void sendAll(proto::MsgType t, const std::string& payload, int skipSeat = -1) {
        for (int i = 0; i < static_cast<int>(peers.size()); ++i) {
            if (i == skipSeat) continue;
            send(i, t, payload);
        }
    }

    void sendLobby() {
        proto::LobbyState st;
        for (int i = 0; i < rules.playerCount; ++i) {
            proto::LobbySeat ls;
            ls.name = names[(size_t)i];
            ls.ready = (i == 0) || bot[(size_t)i] ||
                       peers[(size_t)i].sock != net::kInvalidSocket;
            ls.connected = (i == 0) || peers[(size_t)i].sock != net::kInvalidSocket;
            ls.isBot = bot[(size_t)i];
            st.seats.push_back(ls);
        }
        st.rules = rules;
        st.hostSeat = 0;
        st.botDifficulty = cfg.difficulty;
        st.fillWithBots = true;
        st.chat = lobbyChat;
        sendAll(proto::MsgType::Lobby, proto::makeLobby(st), 0);
    }

    void sendChat(const std::string& from, const std::string& text) {
        sendAll(proto::MsgType::ChatMsg, proto::makeChat(from, text));
    }

    // -- pumping ----------------------------------------------------------
    // Every socket is non-blocking, so a direct accept/recv attempt is the
    // poll: would-block simply means "nothing right now".
    void pump() {
        for (int round = 0; round < 8; ++round) {
            net::Socket s = net::kInvalidSocket;
            if (listener == net::kInvalidSocket) break;
            if (!net::acceptOne(listener, s)) break;
            int seat = -1;
            for (int i = 1; i < rules.playerCount; ++i) {
                if (peers[(size_t)i].sock == net::kInvalidSocket) {
                    seat = i;
                    break;
                }
            }
            if (seat < 0) {
                net::closeSocket(s);
                continue;
            }
            Peer& p = peers[(size_t)seat];
            p.sock = s;
            p.seat = seat;
            p.inBuf.clear();
            p.outBuf.clear();
            p.decoder = proto::FrameDecoder();
            p.actions.clear();
            p.joined = false;
            p.connectedAt = std::chrono::steady_clock::now();
        }

        const auto now = std::chrono::steady_clock::now();
        for (int i = 1; i < rules.playerCount; ++i) {
            Peer& p = peers[(size_t)i];
            if (p.sock == net::kInvalidSocket) continue;

            // Resume any frame that was left behind by a full send buffer.
            flushPeer(i);
            if (p.sock == net::kInvalidSocket) continue;

            // A silent connection must not hold a seat indefinitely.
            if (!p.joined &&
                now - p.connectedAt > std::chrono::milliseconds(kJoinTimeoutMs)) {
                closePeer(i);
                continue;
            }

            if (!net::recvRaw(p.sock, p.inBuf)) {
                closePeer(i);
                continue;
            }
            if (!p.inBuf.empty()) {
                p.decoder.feed(p.inBuf);
                p.inBuf.clear();
            }
            proto::Message m;
            while (p.decoder.next(m)) handle(i, m);
            if (p.sock == net::kInvalidSocket) continue;
            if (p.decoder.failed()) closePeer(i);
        }
    }

    void closePeer(int seat) {
        Peer& p = peers[(size_t)seat];
        if (p.sock == net::kInvalidSocket) return;
        net::closeSocket(p.sock);
        p.sock = net::kInvalidSocket;
        p.outBuf.clear();
        p.actions.clear();
        p.joined = false;
        if (started) {
            // Their seat falls back to a CPU so the match keeps running.
            bot[(size_t)seat] = true;
            names[(size_t)seat] = "CPU " + S(seat + 1);
        }
    }

    void handle(int seat, const proto::Message& m) {
        Peer& p = peers[(size_t)seat];
        switch (m.type) {
            case proto::MsgType::Hello: {
                std::string name;
                uint8_t lang = 0;
                if (proto::parseHello(m.payload, name, lang)) {
                    name = cleanName(name);
                    if (!name.empty()) {
                        p.name = name;
                        p.lang = lang;
                        names[(size_t)seat] = name;
                    }
                }
                break;
            }
            case proto::MsgType::JoinRoom: {
                std::string joinCode, name;
                uint8_t lang = 0;
                if (!proto::parseJoinRoom(m.payload, joinCode, name, lang)) break;
                switch (proto::checkJoin(code, joinCode, started)) {
                    case proto::JoinCheck::BadRoom:
                        send(seat, proto::MsgType::Error,
                             proto::makeError("bad_room", "wrong room code"));
                        closePeer(seat);
                        return;
                    case proto::JoinCheck::InProgress:
                        // Seats are fixed once the match is running; a late
                        // joiner is told so rather than being left waiting for
                        // a turn that will never come.
                        send(seat, proto::MsgType::Error,
                             proto::makeError("in_progress", tr("pvp.already_started")));
                        closePeer(seat);
                        return;
                    case proto::JoinCheck::Ok:
                        break;
                }
                name = cleanName(name);
                if (name.empty()) name = "player" + S(seat + 1);
                p.name = name;
                p.lang = lang;
                p.joined = true;
                names[(size_t)seat] = name;
                bot[(size_t)seat] = false;
                send(seat, proto::MsgType::Welcome,
                     proto::makeWelcome(seat, names[(size_t)seat], code, seat == 0));
                sendLobby();
                break;
            }
            case proto::MsgType::Action: {
                Action a;
                if (proto::parseAction(m.payload, a) && a.type != ActType::None) {
                    if (p.actions.size() < 16) p.actions.push_back(a);
                }
                break;
            }
            case proto::MsgType::Chat: {
                std::string from, text;
                if (!proto::parseChat(m.payload, from, text)) break;
                text = cleanChat(text);
                if (text.empty()) break;
                while (lobbyChat.size() >= 20) lobbyChat.erase(lobbyChat.begin());
                lobbyChat.push_back("<" + names[(size_t)seat] + "> " + text);
                sendChat(names[(size_t)seat], text);
                break;
            }
            case proto::MsgType::Leave:
                closePeer(seat);
                return;
            default:
                break;
        }
    }

    bool pollAction(int seat, Action& out, int timeoutMs) {
        if (seat < 0 || seat >= static_cast<int>(peers.size())) return false;
        for (int waited = 0; waited <= timeoutMs; waited += 10) {
            Peer& p = peers[(size_t)seat];
            if (!p.actions.empty()) {
                out = p.actions.front();
                p.actions.pop_front();
                return true;
            }
            if (p.sock == net::kInvalidSocket) return false;
            if (waited == timeoutMs) break;
            pump();
            net::sleepMs(10);
        }
        return false;
    }

    // -- drawing ----------------------------------------------------------
    void drawLobby() {
        const int w = std::min(consoleWidth(), 88);
        clearScreen();
        println("");
        println(centerTo(std::string(kBold) + kCyan + tr("pvp.lobby") + kR, w));
        println("");
        println("  " + padTo(tr("pvp.room"), 14) + std::string(kBold) + code + kR);
        println("  " + padTo("address", 14) + net::localAddress() + ":" + S(port));
        println("  " + padTo(tr("rules.players"), 14) + rules.describe());
        println("");
        for (int i = 0; i < rules.playerCount; ++i) {
            std::string who = (i == 0) ? (names[(size_t)i] + " (" + tr("common.you") + ")")
                                       : names[(size_t)i];
            std::string state;
            if (i == 0) state = std::string(kGreen) + tr("pvp.host_seat") + kR;
            else if (bot[(size_t)i]) state = std::string(kDim) + "CPU" + kR;
            else state = std::string(kGreen) + tr("pvp.connected") + kR;
            println("  " + padTo(S(i + 1) + ". " + who, 30) + state);
        }
        println("");
        for (const std::string& c : lobbyChat) println("  " + std::string(kDim) + c + kR);
        println("");
        println("  " + std::string(kDim) + "Enter = start   / = chat   Esc = close room" + kR);
        println("  " + std::string(kDim) + "join with: uno --join " +
               (net::localAddress().empty() ? std::string("<host-ip>") : net::localAddress()) +
               " -p " + S(port) + " -c " + code + kR);
    }

    void shutdown() {
        for (Peer& p : peers) {
            if (p.sock != net::kInvalidSocket) net::closeSocket(p.sock);
        }
        if (listener != net::kInvalidSocket) net::closeSocket(listener);
    }
};

void hostLobby(Room& room, GameUI& ui) {
    (void)ui;
    // With no console on stdin nobody can press Enter, so give remote players
    // a short grace period before the CPUs fill the seats.
    const bool unattended = !consoleInteractive();
    int waitedMs = 0;

    for (;;) {
        room.pump();
        room.drawLobby();

        if (unattended) {
            bool anyRemote = false;
            for (int i = 1; i < room.rules.playerCount; ++i) {
                if (!room.bot[(size_t)i]) anyRemote = true;
            }
            if (anyRemote || waitedMs >= kUnattendedGraceMs) {
                room.started = true;
                return;
            }
            waitedMs += 40;
        }

        // Non-blocking key handling so the lobby stays responsive.
        KeyEvent k;
        bool got = false;
        for (int i = 0; i < 8 && !got; ++i) got = pollKey(20, k);
        if (!got) continue;

        if (k.key == Key::CtrlC || k.key == Key::Escape) return;
        if (k.key == Key::Enter) {
            room.started = true;
            return;
        }
        if (k.isChar('/')) {
            const std::string text = cleanChat(readLine(""));
            if (!text.empty()) {
                while (room.lobbyChat.size() >= 20) room.lobbyChat.erase(room.lobbyChat.begin());
                room.lobbyChat.push_back("<" + room.names[0] + "> " + text);
                room.sendChat(room.names[0], text);
            }
        }
    }
}

void runHostedMatch(Room& room, GameUI& ui) {
    MatchSession ms(room.rules, room.cfg.seed ? room.cfg.seed : room.rng());
    const Difficulty diff = difficultyFromInt(room.cfg.difficulty);

    ms.addController(std::make_unique<HumanController>(room.names[0], ui));

    std::vector<RemoteController*> remotes;
    for (int i = 1; i < room.rules.playerCount; ++i) {
        if (room.bot[(size_t)i] || room.peers[(size_t)i].sock == net::kInvalidSocket) {
            ms.addController(std::make_unique<BotController>(room.names[(size_t)i], diff,
                                                            room.rng(), i));
            ms.setSeatBot(i, true);
        } else {
            auto rc = std::make_unique<RemoteController>(room.names[(size_t)i], i);
            remotes.push_back(rc.get());
            ms.addController(std::move(rc));
        }
    }
    for (auto* rc : remotes) {
        rc->setSource([&room](int seat, Action& a) { return room.pollAction(seat, a, 30000); });
    }

    auto sink = [&](int seat, const View& v) {
        for (const Event& e : v.log) ui.logEvent(e, v);
        if (seat == 0) return;
        ByteWriter w;
        writeView(w, v);
        room.send(seat, proto::MsgType::View, w.take());
    };

    auto keepGoing = [&]() {
        room.pump();
        for (int i = 1; i < room.rules.playerCount; ++i) {
            const bool connected = room.peers[(size_t)i].sock != net::kInvalidSocket;
            ms.setSeatConnected(i, connected);
            ms.setSeatBot(i, room.bot[(size_t)i] || !connected);
        }
        return true;
    };

    ms.startGame();
    ms.run(sink, keepGoing);

    // Final snapshot for everyone still connected.
    for (int i = 1; i < room.rules.playerCount; ++i) {
        if (room.peers[(size_t)i].sock == net::kInvalidSocket) continue;
        ByteWriter w;
        writeView(w, ms.game().viewFor(i));
        room.send(i, proto::MsgType::View, w.take());
    }

    showMatchResult(ms.game().viewFor(0));
}

// ===========================================================================
// Client
// ===========================================================================

struct Client {
    net::Socket sock = net::kInvalidSocket;
    std::string inBuf;
    net::OutBuf outBuf;
    proto::FrameDecoder decoder;
    int seat = -1;
    std::string roomCode;
    std::string name;
    uint8_t lang = 0;
    View view;
    bool haveView = false;
    bool matchOver = false;
    std::vector<std::string> chat;

    void flush() {
        if (sock == net::kInvalidSocket) return;
        if (!outBuf.flush(sock)) close();
    }

    void send(proto::MsgType t, const std::string& payload) {
        if (sock == net::kInvalidSocket) return;
        const std::string frame = proto::encode(t, payload);
        if (!outBuf.push(frame)) {
            close();
            return;
        }
        flush();
    }

    // Non-blocking read; recv tells us when nothing more is available.
    void pump() {
        if (sock == net::kInvalidSocket) return;
        flush();
        if (sock == net::kInvalidSocket) return;
        for (;;) {
            const size_t before = inBuf.size();
            if (!net::recvRaw(sock, inBuf)) {
                close();
                return;
            }
            if (inBuf.size() == before) break;  // drained for now
            if (!inBuf.empty()) {
                decoder.feed(inBuf);
                inBuf.clear();
            }
            proto::Message m;
            while (sock != net::kInvalidSocket && decoder.next(m)) handle(m);
            if (sock == net::kInvalidSocket) return;
            if (decoder.failed()) {
                close();
                return;
            }
        }
    }

    void close() {
        if (sock != net::kInvalidSocket) {
            net::closeSocket(sock);
            sock = net::kInvalidSocket;
        }
        outBuf.clear();
    }

    void handle(const proto::Message& m) {
        switch (m.type) {
            case proto::MsgType::Welcome: {
                std::string n, code;
                bool isHost = false;
                int s = -1;
                if (proto::parseWelcome(m.payload, s, n, code, isHost)) {
                    seat = s;
                    roomCode = code;
                }
                break;
            }
            case proto::MsgType::View: {
                if (proto::parseView(m.payload, view)) {
                    // Defence in depth: never render control bytes, even if a
                    // (malicious or buggy) host put them in a snapshot.
                    for (PlayerInfo& pi : view.players) {
                        pi.name = proto::sanitizeText(pi.name, proto::kMaxNameCodepoints);
                    }
                    haveView = true;
                    if (view.matchOver) matchOver = true;
                }
                break;
            }
            case proto::MsgType::ChatMsg: {
                std::string from, text;
                if (proto::parseChat(m.payload, from, text)) {
                    from = cleanName(from);
                    text = cleanChat(text);
                    if (!text.empty()) chat.push_back("<" + from + "> " + text);
                }
                break;
            }
            case proto::MsgType::Error: {
                std::string code, msg;
                if (proto::parseError(m.payload, code, msg)) {
                    chat.push_back("! " + proto::truncateUtf8(
                                            proto::sanitizeText(msg, proto::kMaxChatBytes),
                                            proto::kMaxChatBytes));
                }
                break;
            }
            default:
                break;
        }
    }
};

void clientWaitingRoom(Client& cli, GameUI& ui) {
    while (cli.sock != net::kInvalidSocket) {
        cli.pump();
        const int w = std::min(consoleWidth(), 80);
        clearScreen();
        println("");
        println(centerTo(std::string(kBold) + kCyan + tr("pvp.lobby") + kR, w));
        println("");
        println("  " + tr("pvp.wait_host"));
        println("");
        for (const std::string& c : cli.chat) println("  " + std::string(kDim) + c + kR);
        println("");
        println("  " + std::string(kDim) + "/ = chat   Esc = leave" + kR);

        KeyEvent k;
        bool got = false;
        for (int i = 0; i < 8 && !got; ++i) got = pollKey(20, k);
        if (!got) {
            // Nothing can ever be typed here; fall through to the match.
            if (!consoleInteractive()) return;
            continue;
        }
        if (k.key == Key::CtrlC || k.key == Key::Escape) return;
        if (k.isChar('/')) {
            const std::string text = cleanChat(readLine(""));
            if (!text.empty()) cli.send(proto::MsgType::Chat, proto::makeChat(cli.name, text));
        }
    }
    (void)ui;
}

void runClientMatch(Client& cli, GameUI& ui) {
    ui.setLocalSeat(cli.seat);

    while (cli.sock != net::kInvalidSocket) {
        // Wait for the next snapshot from the host.
        while (!cli.haveView && cli.sock != net::kInvalidSocket) {
            net::sleepMs(5);
            cli.pump();
            KeyEvent k;
            if (pollKey(30, k)) {
                if (k.key == Key::CtrlC) {
                    cli.send(proto::MsgType::Leave, "");
                    cli.close();
                    return;
                }
                if (k.isChar('/')) {
                    const std::string text = cleanChat(readLine(""));
                    if (!text.empty()) cli.send(proto::MsgType::Chat, proto::makeChat(cli.name, text));
                }
            }
        }
        if (cli.sock == net::kInvalidSocket) break;

        const View v = cli.view;
        cli.haveView = false;

        for (const Event& e : v.log) ui.logEvent(e, v);
        for (const std::string& c : cli.chat) ui.pushChat("net", c);
        cli.chat.clear();

        if (v.matchOver) {
            showMatchResult(v);
            break;
        }

        const bool mine = (v.current == cli.seat);
        if (mine) {
            const Action a = ui.promptAction(v);
            if (a.type == ActType::None) {
                cli.send(proto::MsgType::Leave, "");
                cli.close();
                return;
            }
            cli.send(proto::MsgType::Action, proto::makeAction(a));
        } else {
            ui.drawBoard(v);
            ui.drawWaiting(v);
        }
    }

    if (cli.sock == net::kInvalidSocket) {
        clearScreen();
        println(std::string(kYellow) + tr("pvp.host_left") + kR);
        println(tr("common.press_enter"));
        waitKey();
    }
}

}  // namespace

// ===========================================================================
// Public entry points
// ===========================================================================

void hostOnlineGame(const AppConfig& cfg) {
    const uint16_t port = cfg.port ? cfg.port : askPort();

    Room room;
    room.cfg = cfg;
    room.rules = cfg.rules;
    room.rng.seed(cfg.seed ? cfg.seed
                           : static_cast<uint32_t>(std::chrono::steady_clock::now()
                                                      .time_since_epoch()
                                                      .count()));
    room.code = makeCode(room.rng);
    room.port = port;
    room.initSeats();
    room.names[0] = cleanName(cfg.playerName);
    if (room.names[0].empty()) room.names[0] = "host";

    if (!room.openPort(port)) {
        clearScreen();
        println(std::string(kYellow) + "cannot bind port " + S(port) + kR);
        println(tr("common.press_enter"));
        waitKey();
        return;
    }

    GameUI ui;
    ui.setLocalSeat(0);
    room.ui = &ui;

    hostLobby(room, ui);
    if (room.started) runHostedMatch(room, ui);
    room.shutdown();
}

void joinOnlineGame(const AppConfig& cfg) {
    const std::string host = cfg.joinHost.empty() ? askText("Host address: ", "127.0.0.1")
                                                   : cfg.joinHost;
    const uint16_t port = cfg.port ? cfg.port : askPort();
    const std::string code =
        proto::sanitizeText(cfg.joinCode.empty() ? readLine("Room code: ") : cfg.joinCode, 16);
    std::string name = cleanName(cfg.playerName.empty()
                                     ? askText(tr("common.enter_name"),
                                               "player" + S(static_cast<int>(std::rand() % 900 + 100)))
                                     : cfg.playerName);
    if (name.empty()) name = "player";

    Client cli;
    cli.name = name;
    cli.lang = langCode(cfg.lang);
    std::string err;
    if (!net::connectTo(host, port, cli.sock, &err)) {
        clearScreen();
        println(std::string(kYellow) + tr("pvp.disconnected") + ": " + err + kR);
        println(tr("common.press_enter"));
        waitKey();
        return;
    }

    // Adopt the host's language if they broadcast one.
    cli.send(proto::MsgType::JoinRoom, proto::makeJoinRoom(code, name, cli.lang));

    GameUI ui;
    ui.setLocalSeat(0);

    // Wait for the welcome message that assigns our seat.
    for (int i = 0; i < 200; ++i) {
        cli.pump();
        if (cli.seat >= 0) break;
        if (cli.sock == net::kInvalidSocket) break;
        KeyEvent k;
        if (pollKey(25, k) && k.key == Key::CtrlC) {
            cli.close();
            return;
        }
    }

    if (cli.sock == net::kInvalidSocket) {
        clearScreen();
        println(std::string(kYellow) + tr("pvp.host_left") + kR);
        println(tr("common.press_enter"));
        waitKey();
        return;
    }

    ui.setLocalSeat(cli.seat);
    clientWaitingRoom(cli, ui);
    if (cli.sock != net::kInvalidSocket) runClientMatch(cli, ui);
    cli.close();
}

}  // namespace uno