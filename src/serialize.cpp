#include "uno/serialize.hpp"

#include <cstring>

namespace uno {

// ---------------------------------------------------------------------------
// ByteWriter
// ---------------------------------------------------------------------------

void ByteWriter::u8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }

void ByteWriter::u16(uint16_t v) {
    u8(static_cast<uint8_t>(v & 0xff));
    u8(static_cast<uint8_t>((v >> 8) & 0xff));
}

void ByteWriter::u32(uint32_t v) {
    u16(static_cast<uint16_t>(v & 0xffff));
    u16(static_cast<uint16_t>((v >> 16) & 0xffff));
}

void ByteWriter::i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

void ByteWriter::str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    if (!s.empty()) buf_.append(s);
}

void ByteWriter::bytes(const void* p, size_t n) {
    u32(static_cast<uint32_t>(n));
    if (n) buf_.append(static_cast<const char*>(p), n);
}

void ByteWriter::card(const Card& c) {
    u8(static_cast<uint8_t>(c.color));
    u8(static_cast<uint8_t>(c.kind));
    u8(static_cast<uint8_t>(c.value < 0 ? 255 : c.value));
}

void ByteWriter::rules(const Rules& r) {
    u8(static_cast<uint8_t>(r.playerCount));
    u8(static_cast<uint8_t>(r.initialCards));
    u16(static_cast<uint16_t>(r.targetScore));
    u8(r.stackDrawTwo ? 1 : 0);
    u8(r.wildFourChallenge ? 1 : 0);
    u8(r.wildFourStack ? 1 : 0);
    u8(r.mustPlay ? 1 : 0);
    u8(r.recycleDiscard ? 1 : 0);
    u8(r.unoPenalty ? 1 : 0);
    u8(r.swapHands ? 1 : 0);
    u8(r.teams ? 1 : 0);
}

void ByteWriter::action(const Action& a) {
    u8(static_cast<uint8_t>(a.type));
    i32(a.index);
    u8(static_cast<uint8_t>(a.color));
    i32(a.target);
}

void ByteWriter::playerInfo(const PlayerInfo& p) {
    u8(static_cast<uint8_t>(p.id));
    str(p.name);
    u8(static_cast<uint8_t>(p.handSize));
    i32(p.score);
    u8(p.unoCalled ? 1 : 0);
    u8(p.connected ? 1 : 0);
    u8(p.isBot ? 1 : 0);
}

void ByteWriter::event(const Event& e) {
    u8(static_cast<uint8_t>(e.type));
    i32(e.player);
    i32(e.value);
    u8(static_cast<uint8_t>(e.color));
    card(e.card);
    u8(static_cast<uint8_t>(e.cards.size()));
    for (const Card& c : e.cards) card(c);
    str(e.text);
}

// ---------------------------------------------------------------------------
// ByteReader
// ---------------------------------------------------------------------------

uint8_t ByteReader::u8() {
    if (!need(1)) return 0;
    return static_cast<uint8_t>(*p_++);
}

uint16_t ByteReader::u16() {
    const uint16_t a = u8();
    const uint16_t b = u8();
    return static_cast<uint16_t>(a | (b << 8));
}

uint32_t ByteReader::u32() {
    const uint32_t a = u16();
    const uint32_t b = u16();
    return a | (b << 16);
}

int32_t ByteReader::i32() { return static_cast<int32_t>(u32()); }

std::string ByteReader::str() {
    const uint32_t n = u32();
    if (!need(n)) return {};
    std::string s(p_, p_ + n);
    p_ += n;
    return s;
}

void ByteReader::bytes(void* dst, size_t n) {
    const uint32_t len = u32();
    if (!need(len)) return;
    const size_t take = (len < n) ? len : n;
    if (dst && take) std::memcpy(dst, p_, take);
    p_ += len;
}

Card ByteReader::card() {
    Card c;
    c.color = static_cast<Color>(u8());
    c.kind = static_cast<Kind>(u8());
    const uint8_t v = u8();
    c.value = (v == 255) ? -1 : static_cast<int>(v);
    return c;
}

Rules ByteReader::rules() {
    Rules r;
    r.playerCount = u8();
    r.initialCards = u8();
    r.targetScore = u16();
    r.stackDrawTwo = u8() != 0;
    r.wildFourChallenge = u8() != 0;
    r.wildFourStack = u8() != 0;
    r.mustPlay = u8() != 0;
    r.recycleDiscard = u8() != 0;
    r.unoPenalty = u8() != 0;
    r.swapHands = u8() != 0;
    r.teams = u8() != 0;
    return r;
}

Action ByteReader::action() {
    Action a;
    a.type = static_cast<ActType>(u8());
    a.index = i32();
    a.color = static_cast<Color>(u8());
    a.target = i32();
    return a;
}

PlayerInfo ByteReader::playerInfo() {
    PlayerInfo p;
    p.id = u8();
    p.name = str();
    p.handSize = u8();
    p.score = i32();
    p.unoCalled = u8() != 0;
    p.connected = u8() != 0;
    p.isBot = u8() != 0;
    return p;
}

Event ByteReader::event() {
    Event e;
    e.type = static_cast<EvType>(u8());
    e.player = i32();
    e.value = i32();
    e.color = static_cast<Color>(u8());
    e.card = card();
    const uint8_t n = u8();
    for (uint8_t i = 0; i < n && ok_; ++i) e.cards.push_back(card());
    e.text = str();
    return e;
}

// ---------------------------------------------------------------------------
// View
// ---------------------------------------------------------------------------

void writeView(ByteWriter& w, const View& v) {
    w.u8(1);  // format version

    w.u16(static_cast<uint16_t>(v.myId));
    w.u16(static_cast<uint16_t>(v.playerCount));
    w.u8(static_cast<uint8_t>(v.phase));
    w.u16(static_cast<uint16_t>(v.current));
    w.u8(static_cast<uint8_t>(v.direction < 0 ? 255 : 1));
    w.u16(static_cast<uint16_t>(v.turnNo));
    w.u16(static_cast<uint16_t>(v.roundNo));
    w.u8(v.matchOver ? 1 : 0);
    w.card(v.top);
    w.u8(static_cast<uint8_t>(v.activeColor));
    w.u8(static_cast<uint8_t>(v.claimedColor));

    w.u16(static_cast<uint16_t>(v.players.size()));
    for (const PlayerInfo& p : v.players) w.playerInfo(p);

    w.u16(static_cast<uint16_t>(v.hand.size()));
    for (const Card& c : v.hand) w.card(c);

    w.card(v.justDrawn);
    w.u8(v.hasJustDrawn ? 1 : 0);

    w.u16(static_cast<uint16_t>(v.pendingDraw));
    w.card(v.pendingCard);
    w.u8(static_cast<uint8_t>(v.blockedColor));
    w.u8(v.challengeOpen ? 1 : 0);
    w.u8(v.canStack ? 1 : 0);
    w.u8(v.canPass ? 1 : 0);
    w.u8(v.canDraw ? 1 : 0);
    w.u8(v.canPlay ? 1 : 0);
    w.u8(v.holdsActiveColor ? 1 : 0);
    w.u8(v.callUnoAvailable ? 1 : 0);
    w.i32(v.catchTarget);

    w.u16(static_cast<uint16_t>(v.drawCount));
    w.u16(static_cast<uint16_t>(v.discardCount));

    w.u16(static_cast<uint16_t>(v.winners.size()));
    for (int x : v.winners) w.u16(static_cast<uint16_t>(x));
    w.u16(static_cast<uint16_t>(v.finalRank.size()));
    for (int x : v.finalRank) w.u16(static_cast<uint16_t>(x));

    w.rules(v.rules);

    w.u16(static_cast<uint16_t>(v.log.size()));
    for (const Event& e : v.log) w.event(e);
}

bool readView(ByteReader& r, View& v) {
    const uint8_t version = r.u8();
    if (version != 1 || !r.ok()) return false;

    v.myId = r.u16();
    v.playerCount = r.u16();
    v.phase = static_cast<Phase>(r.u8());
    v.current = r.u16();
    v.direction = r.u8() == 255 ? -1 : 1;
    v.turnNo = r.u16();
    v.roundNo = r.u16();
    v.matchOver = r.u8() != 0;
    v.top = r.card();
    v.activeColor = static_cast<Color>(r.u8());
    v.claimedColor = static_cast<Color>(r.u8());

    const uint16_t np = r.u16();
    v.players.clear();
    v.players.reserve(np);
    for (uint16_t i = 0; i < np && r.ok(); ++i) v.players.push_back(r.playerInfo());

    const uint16_t nh = r.u16();
    v.hand.clear();
    v.hand.reserve(nh);
    for (uint16_t i = 0; i < nh && r.ok(); ++i) v.hand.push_back(r.card());

    v.justDrawn = r.card();
    v.hasJustDrawn = r.u8() != 0;

    v.pendingDraw = r.u16();
    v.pendingCard = r.card();
    v.blockedColor = static_cast<Color>(r.u8());
    v.challengeOpen = r.u8() != 0;
    v.canStack = r.u8() != 0;
    v.canPass = r.u8() != 0;
    v.canDraw = r.u8() != 0;
    v.canPlay = r.u8() != 0;
    v.holdsActiveColor = r.u8() != 0;
    v.callUnoAvailable = r.u8() != 0;
    v.catchTarget = r.i32();

    v.drawCount = r.u16();
    v.discardCount = r.u16();

    const uint16_t nw = r.u16();
    v.winners.clear();
    for (uint16_t i = 0; i < nw && r.ok(); ++i) v.winners.push_back(r.u16());
    const uint16_t nr = r.u16();
    v.finalRank.clear();
    for (uint16_t i = 0; i < nr && r.ok(); ++i) v.finalRank.push_back(r.u16());

    v.rules = r.rules();

    const uint16_t nl = r.u16();
    v.log.clear();
    v.log.reserve(nl);
    for (uint16_t i = 0; i < nl && r.ok(); ++i) v.log.push_back(r.event());

    return r.ok();
}

}  // namespace uno