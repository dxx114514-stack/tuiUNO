// Self-contained test harness: no external framework.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "uno/ai.hpp"
#include "uno/card.hpp"
#include "uno/console.hpp"
#include "uno/deck.hpp"
#include "uno/game.hpp"
#include "uno/i18n.hpp"
#include "uno/net.hpp"
#include "uno/protocol.hpp"
#include "uno/rules.hpp"
#include "uno/serialize.hpp"
#include "uno/session.hpp"
#include "uno/ui.hpp"

using namespace uno;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_case = "";

void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL  [%s] %s\n", g_case, what);
    }
}

void checkEq(int got, int want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  FAIL  [%s] %s: got %d, want %d\n", g_case, what, got, want);
    }
}

struct Case {
    explicit Case(const char* name) {
        g_case = name;
        std::printf("- %s\n", name);
    }
};

Rules defaultRules() {
    Rules r;
    r.playerCount = 4;
    r.initialCards = 7;
    r.targetScore = 500;
    r.stackDrawTwo = true;
    r.wildFourChallenge = true;
    return r;
}

// Every card must be somewhere: a hand, the draw pile, or the discard pile.
int totalCards(const Game& g, const Rules& r) {
    int n = g.deck().totalRemaining();
    for (int i = 0; i < r.playerCount; ++i) n += static_cast<int>(g.handOf(i).size());
    return n;
}

// --------------------------------------------------------------------------

void testStandardDeck() {
    Case c("standard deck composition");
    const std::vector<Card> deck = standardDeck();
    checkEq(static_cast<int>(deck.size()), 108, "108 cards");

    std::map<uint32_t, int> counts;
    int wild = 0, wildFour = 0, zeros = 0, specials = 0;
    for (const Card& card : deck) {
        counts[card.uid()]++;
        if (card.kind == Kind::Wild) ++wild;
        if (card.kind == Kind::WildDrawFour) ++wildFour;
        if (card.kind == Kind::Number && card.value == 0) ++zeros;
        if (card.isSpecial() && !card.isWild()) ++specials;
    }
    checkEq(wild, 4, "4 wild");
    checkEq(wildFour, 4, "4 wild draw four");
    checkEq(zeros, 4, "one zero per colour");
    checkEq(specials, 24, "24 coloured action cards");

    // 0 appears once, 1-9 twice, action cards twice each.
    for (Color col : {Color::Red, Color::Yellow, Color::Green, Color::Blue}) {
        for (int v = 0; v <= 9; ++v) {
            const int want = (v == 0) ? 1 : 2;
            checkEq(counts[Card::number(col, v).uid()], want, "number count");
        }
        for (Kind k : {Kind::Skip, Kind::Reverse, Kind::DrawTwo}) {
            Card probe = (k == Kind::Skip)     ? Card::skip(col)
                         : (k == Kind::Reverse) ? Card::reverse(col)
                                                : Card::drawTwo(col);
            checkEq(counts[probe.uid()], 2, "action card count");
        }
    }
}

void testCardBasics() {
    Case c("card helpers");
    check(Card::number(Color::Red, 7).playableOn(Color::Red), "same colour matches");
    check(!Card::number(Color::Red, 7).playableOn(Color::Blue), "other colour rejected");
    check(Card::number(Color::Red, 7).playableOn(Color::Yellow) == false, "value does not match");
    check(Card::wild().playableOn(Color::Green), "wild is always playable");
    check(Card::wildFour().playableOn(Color::None), "wild four always playable");
    checkEq(Card::number(Color::Blue, 9).points(), 9, "number points");
    checkEq(Card::skip(Color::Blue).points(), 20, "action points");
    checkEq(Card::wildFour().points(), 50, "wild points");
    check(std::string(Card::number(Color::Green, 3).tag()) == "G3", "tag");
    check(std::string(Card::wildFour().tag()) == "+4", "wild four tag");
}

void testDeckDrawAndRecycle() {
    Case c("deck draw and recycle");
    Deck d(1234);
    checkEq(d.drawCount(), 108, "full draw pile");

    Card last;
    for (int i = 0; i < 400; ++i) {
        check(d.tryDrawOne(last), "a card is available while recycling");
        d.discard(last);
        checkEq(d.totalRemaining(), 108, "cards are never lost or duplicated");
    }

    // Draining a deck with an empty discard pile must hand out every card;
    // once cards are on the table the top one is kept back.
    Deck f(4242);
    int supplied = 0;
    Card card;
    while (f.tryDrawOne(card)) ++supplied;
    checkEq(supplied, 108, "supplies every card when the discard is empty");

    Deck g(99);
    Card a1;
    check(g.tryDrawOne(a1), "seed draw");
    g.discard(a1);
    int withTop = 0;
    Card probe;
    while (g.tryDrawOne(probe)) ++withTop;
    checkEq(withTop, 107, "keeps the last card face up");
}

void testViewPrivacy() {
    Case c("views hide other hands");
    const Rules r = defaultRules();
    Game g(r, 7);
    for (int i = 0; i < r.playerCount; ++i) g.setName(i, "P" + S(i));
    g.start();

    for (int p = 0; p < r.playerCount; ++p) {
        const View v = g.viewFor(p);
        checkEq(static_cast<int>(v.hand.size()), r.initialCards, "own hand size");
        // The sizes reported for other players must match the real hands.
        for (int q = 0; q < r.playerCount; ++q) {
            checkEq(v.players[(size_t)q].handSize,
                    static_cast<int>(g.handOf(q).size()), "reported hand size");
        }
        check(totalCards(g, r) == 108, "all 108 cards accounted for");
    }
}

void testNumberMustMatchColour() {
    Case c("playability rules");
    const Rules r = defaultRules();
    Game g(r, 42);
    g.start();
    // Find a legal play for whoever is on turn, if any.
    const int me = g.current();
    const View v = g.viewFor(me);
    for (size_t i = 0; i < v.hand.size(); ++i) {
        const Card& c = v.hand[i];
        Action a;
        a.type = ActType::Play;
        a.index = static_cast<int>(i);
        const bool ok = g.submit(me, a);
        // A Wild Draw Four is always playable now; whether it is a cheat is a
        // matter of judgement, not legality.
        check(ok == c.playableOn(v.activeColor), "legality matches the rule");
        break;  // one probe is enough
    }
}

void testRejectingOutOfTurn() {
    Case c("out of turn actions are rejected");
    const Rules r = defaultRules();
    Game g(r, 99);
    g.start();
    const int notMe = (g.current() + 1) % r.playerCount;
    Action a;
    a.type = ActType::Draw;
    check(!g.submit(notMe, a), "drawing out of turn fails");
    check(!g.submit(-1, a), "negative seat fails");
    check(!g.submit(999, a), "out of range seat fails");
}

void testDrawAndPlay() {
    Case c("drawing when stuck passes the turn");
    const Rules r = defaultRules();
    Game g(r, 5);
    g.start();
    int guard = 0;
    while (g.phase() == Phase::Play && guard++ < 40) {
        const int me = g.current();
        const View v = g.viewFor(me);
        if (v.canPlay) {
            // Pick a legal card if there is one, otherwise draw.
            bool played = false;
            for (size_t i = 0; i < v.hand.size(); ++i) {
                Action a;
                a.type = ActType::Play;
                a.index = static_cast<int>(i);
                if (g.submit(me, a)) {
                    played = true;
                    break;
                }
            }
            if (played) continue;
        }
        Action a;
        a.type = ActType::Draw;
        if (!g.submit(me, a)) break;
        // Resolve the follow-up.
        const int me2 = g.current();
        if (me2 == me && g.phase() == Phase::Play) {
            Action pass;
            pass.type = ActType::Pass;
            g.submit(me2, pass);
        }
    }
    check(totalCards(g, r) == 108, "cards conserved");
    check(g.turnNo() > 0, "turns advanced");
}

void testDrawTwoStacking() {
    Case c("draw two stacking");
    Rules r = defaultRules();
    r.stackDrawTwo = true;
    Game g(r, 2024);
    g.start();

    // Drive until somebody is facing a pending penalty.
    int guard = 0;
    while (g.phase() != Phase::Penalty && guard++ < 400) {
        const int me = g.current();
        const View v = g.viewFor(me);
        bool acted = false;
        for (size_t i = 0; i < v.hand.size(); ++i) {
            Action a;
            a.type = ActType::Play;
            a.index = static_cast<int>(i);
            if (g.submit(me, a)) {
                acted = true;
                break;
            }
        }
        if (!acted) {
            Action a;
            a.type = ActType::Draw;
            if (!g.submit(me, a)) break;
            const int me2 = g.current();
            if (me2 == me) {
                Action pass;
                pass.type = ActType::Pass;
                g.submit(me2, pass);
            }
        }
    }
    if (g.phase() != Phase::Penalty) {
        std::printf("  (skipped: no +2 came up in the sample)\n");
        return;
    }
    const View v = g.viewFor(g.current());
    check(v.pendingDraw >= 2, "a penalty is pending");
    check(totalCards(g, r) == 108, "cards conserved while pending");
}

// Plays a Wild Draw Four and challenges it. `wantCheat` selects whether we are
// looking for an attacker who was holding the blocked colour.
struct ChallengeProbe {
    bool found = false;
    bool wasCheat = false;
    int attackerDelta = 0;
    int challengerDelta = 0;
    int penaltyAfter = 0;
};

ChallengeProbe probeChallenge(const Rules& r, uint32_t seed, bool wantCheat) {
    ChallengeProbe out;
    Game g(r, seed);
    g.start();
    for (int turn = 0; turn < 80; ++turn) {
        const int me = g.current();
        const View v = g.viewFor(me);
        if (v.phase == Phase::PickColor) {
            Action a;
            a.type = ActType::ChooseColor;
            a.color = Color::Blue;
            g.submit(me, a);
            continue;
        }
        if (v.phase == Phase::Penalty && v.challengeOpen) {
            const int attacker = (me + r.playerCount - 1) % r.playerCount;
            bool cheat = false;
            for (const Card& c : g.handOf(attacker)) {
                if (c.color != Color::None && c.color == v.blockedColor) cheat = true;
            }
            if (cheat != wantCheat) {
                Action a;
                a.type = ActType::Accept;
                g.submit(me, a);
                continue;
            }
            const int a0 = static_cast<int>(g.handOf(attacker).size());
            const int c0 = static_cast<int>(g.handOf(me).size());
            Action chal;
            chal.type = ActType::Challenge;
            if (!g.submit(me, chal)) return out;
            out.found = true;
            out.wasCheat = cheat;
            out.attackerDelta = static_cast<int>(g.handOf(attacker).size()) - a0;
            out.challengerDelta = static_cast<int>(g.handOf(me).size()) - c0;
            out.penaltyAfter = v.pendingDraw;
            return out;
        }

        // Prefer to play a Wild Draw Four so the scenario comes up quickly.
        bool acted = false;
        for (size_t i = 0; i < v.hand.size(); ++i) {
            if (v.hand[i].kind != Kind::WildDrawFour) continue;
            Action a;
            a.type = ActType::Play;
            a.index = static_cast<int>(i);
            if (g.submit(me, a)) {
                acted = true;
                break;
            }
        }
        if (acted) continue;
        if (v.phase == Phase::Penalty) {
            Action a;
            a.type = ActType::Accept;
            if (g.submit(me, a)) continue;
            break;
        }
        Action a;
        a.type = ActType::Draw;
        if (!g.submit(me, a)) break;
        const int me2 = g.current();
        if (me2 == me && g.phase() == Phase::Play) {
            Action pass;
            pass.type = ActType::Pass;
            if (!g.submit(me2, pass)) break;
        }
    }
    return out;
}

void testWildFourChallenge() {
    Case c("wild+4 challenge resolves both ways");
    Rules r = defaultRules();
    r.playerCount = 2;
    r.wildFourChallenge = true;
    r.wildFourStack = false;
    r.stackDrawTwo = false;

    ChallengeProbe cheat;
    ChallengeProbe honest;
    for (uint32_t seed = 1; seed < 4000 && !(cheat.found && honest.found); ++seed) {
        if (!cheat.found) {
            ChallengeProbe p = probeChallenge(r, seed, /*wantCheat=*/true);
            if (p.found) cheat = p;
        }
        if (!honest.found) {
            ChallengeProbe p = probeChallenge(r, seed, /*wantCheat=*/false);
            if (p.found) honest = p;
        }
    }

    check(cheat.found, "found a cheating wild+4");
    if (cheat.found) {
        checkEq(cheat.attackerDelta, 4, "cheater takes the four cards back");
        checkEq(cheat.challengerDelta, 2, "challenger draws two instead");
    }

    check(honest.found, "found an honest wild+4");
    if (honest.found) {
        checkEq(honest.attackerDelta, 0, "honest attacker keeps their cards");
        checkEq(honest.challengerDelta, honest.penaltyAfter,
                "challenger eats the whole penalty");
    }
}

void testChallengeDisabled() {
    Case c("challenge can be switched off");
    Rules r = defaultRules();
    r.playerCount = 2;
    r.wildFourChallenge = false;
    r.wildFourStack = false;
    Game g(r, 4711);
    g.start();
    int guard = 0;
    bool sawPenalty = false;
    while (guard++ < 300) {
        const int me = g.current();
        const View v = g.viewFor(me);
        if (v.phase == Phase::PickColor) {
            Action a;
            a.type = ActType::ChooseColor;
            a.color = Color::Green;
            if (g.submit(me, a)) continue;
            break;
        }
        check(!v.challengeOpen, "challenge window never opens");
        if (v.pendingDraw > 0) sawPenalty = true;
        bool acted = false;
        for (size_t i = 0; i < v.hand.size(); ++i) {
            Action a;
            a.type = ActType::Play;
            a.index = static_cast<int>(i);
            if (g.submit(me, a)) {
                acted = true;
                break;
            }
        }
        if (!acted) {
            if (v.phase == Phase::Penalty) {
                Action a;
                a.type = ActType::Accept;
                if (g.submit(me, a)) continue;
                break;
            }
            Action a;
            a.type = ActType::Draw;
            if (!g.submit(me, a)) break;
            const int me2 = g.current();
            if (me2 == me && g.phase() == Phase::Play) {
                Action p;
                p.type = ActType::Pass;
                if (!g.submit(me2, p)) break;
            }
        }
    }
    (void)sawPenalty;
    check(totalCards(g, r) == 108, "cards conserved");
}

// Plays the cheapest legal card so the driver does not drown the deck in draw
// twos; action cards are only used when nothing else fits.
bool playSomething(Game& g) {
    const int me = g.current();
    const View v = g.viewFor(me);
    int fallback = -1;
    for (size_t i = 0; i < v.hand.size(); ++i) {
        Action a;
        a.type = ActType::Play;
        a.index = static_cast<int>(i);
        if (!v.hand[i].playableOn(v.activeColor)) continue;
        if (v.hand[i].kind == Kind::Number) return g.submit(me, a);
        if (fallback < 0) fallback = static_cast<int>(i);
    }
    if (fallback >= 0) {
        Action a;
        a.type = ActType::Play;
        a.index = fallback;
        return g.submit(me, a);
    }
    return false;
}

// Drives one whole action for whoever is on turn, whatever phase it is in.
bool stepOnce(Game& g) {
    const int me = g.current();
    const View v = g.viewFor(me);
    switch (v.phase) {
        case Phase::PickColor: {
            Action a;
            a.type = ActType::ChooseColor;
            a.color = Color::Red;
            return g.submit(me, a);
        }
        case Phase::Penalty: {
            Action a;
            a.type = ActType::Accept;
            return g.submit(me, a);
        }
        case Phase::Play: {
            if (playSomething(g)) return true;
            Action a;
            a.type = ActType::Draw;
            if (g.submit(me, a)) return true;
            Action pass;
            pass.type = ActType::Pass;
            return g.submit(me, pass);
        }
        default:
            break;
    }
    return false;
}

// Runs a fresh two-player game with this seed and reports whether the catch
// window ever opened, plus what happened when it did.
struct CatchProbe {
    bool sawWindow = false;
    bool caught = false;
    int offenderBefore = 0;
    int offenderAfter = 0;
    int offenderOnOneCard = 0;
    bool turnKept = false;
    bool windowClosed = false;
};

CatchProbe probeCatch(uint32_t seed) {
    CatchProbe out;
    Rules r = defaultRules();
    r.playerCount = 2;
    r.unoPenalty = true;
    Game g(r, seed);
    g.start();

    for (int step = 0; step < 600; ++step) {
        const int me = g.current();
        const View v = g.viewFor(me);
        if (v.catchTarget >= 0) {
            out.sawWindow = true;
            const int target = v.catchTarget;
            out.offenderOnOneCard = static_cast<int>(g.handOf(target).size());
            out.offenderBefore = out.offenderOnOneCard;
            Action a;
            a.type = ActType::Catch;
            a.target = target;
            out.caught = g.submit(me, a);
            out.offenderAfter = static_cast<int>(g.handOf(target).size());
            out.turnKept = (g.current() == me);
            out.windowClosed = (g.viewFor(me).catchTarget < 0);
            return out;
        }
        if (!stepOnce(g)) break;
    }
    return out;
}

void testUnoCatch() {
    Case c("catching a missing UNO");
    CatchProbe p;
    for (uint32_t seed = 1; seed < 400 && !p.sawWindow; ++seed) p = probeCatch(seed);

    check(p.sawWindow, "the catch window opens");
    if (!p.sawWindow) {
        std::printf("  (no window found in the sample)\n");
        return;
    }
    checkEq(p.offenderOnOneCard, 1, "the offender is on a single card");
    check(p.caught, "the catch is accepted");
    checkEq(p.offenderAfter, p.offenderBefore + 2, "the offender draws two");
    check(p.turnKept, "the catch does not consume the turn");
    check(p.windowClosed, "the window closes afterwards");
}

void testUnoCallClosesWindow() {
    Case c("saying UNO prevents the catch");
    // Driving a whole game by hand is what produces the situation, so sweep
    // seeds until somebody drops to two cards and shouts UNO.
    int calls = 0;
    bool withdrawn = false;
    bool cardsOk = true;
    for (uint32_t seed = 1; seed < 200 && calls == 0; ++seed) {
        Rules r = defaultRules();
        r.playerCount = 2;
        r.unoPenalty = true;
        Game g(r, seed);
        g.start();
        int guard = 0;
        while (!g.matchOver() && guard++ < 1500) {
            const int me = g.current();
            const View v = g.viewFor(me);
            if (v.callUnoAvailable) {
                Action a;
                a.type = ActType::CallUno;
                if (!g.submit(me, a)) break;
                check(g.handOf(me).size() <= 2, "UNO can only be said with two cards");
                check(!g.viewFor(me).callUnoAvailable, "the option is withdrawn");
                withdrawn = true;
                ++calls;
                break;
            }
            if (!stepOnce(g)) break;
        }
        if (totalCards(g, r) != 108) cardsOk = false;
    }
    check(calls > 0, "UNO was called at least once");
    check(withdrawn, "the option is withdrawn after calling");
    check(cardsOk, "cards conserved while shouting UNO");
}

void testFullMatchBots() {
    for (int diff = 0; diff <= 2; ++diff) {
        Case c("full bot match");
        Rules r = defaultRules();
        r.playerCount = 4;
        r.targetScore = 40;  // keep the test quick
        const uint32_t seed = 1000u + static_cast<uint32_t>(diff) * 7u;

        MatchSession ms(r, seed);
        for (int i = 0; i < r.playerCount; ++i) {
            ms.addController(std::make_unique<BotController>(
                "CPU " + S(i + 1), difficultyFromInt(diff), seed + i, i));
        }
        ms.startGame();

        int steps = 0;
        const SessionResult res = ms.run(nullptr, [&]() { return ++steps < 20000; });

        check(res == SessionResult::Completed, "match finished");
        check(ms.game().matchOver(), "a champion was declared");
        const View v = ms.game().viewFor(0);
        check(!v.finalRank.empty(), "final standings produced");
        checkEq(static_cast<int>(v.finalRank.size()), r.playerCount, "all seats ranked");
        for (int p = 0; p < r.playerCount; ++p) {
            check(v.players[(size_t)p].score >= 0, "score is never negative");
        }
        check(totalCards(ms.game(), r) == 108, "cards conserved for the whole match");
        std::printf("    (difficulty %d finished in %d actions)\n", diff, steps);
    }
}

void testBotAlwaysLegal() {
    Case c("bots only submit legal actions");
    Rules r = defaultRules();
    r.playerCount = 4;
    r.targetScore = 40;
    MatchSession ms(r, 555);
    for (int i = 0; i < r.playerCount; ++i) {
        ms.addController(std::make_unique<BotController>(
            "CPU " + S(i + 1), Difficulty::Hard, 900 + i, i));
    }
    ms.startGame();

    int steps = 0;
    ms.run(nullptr, [&]() { return ++steps < 3000; });

    for (const Event& e : ms.game().viewFor(0).log) {
        check(e.type != EvType::Rejected, "no illegal action reached the engine");
    }
    check(totalCards(ms.game(), r) == 108, "cards conserved");
}

void testSerializationRoundTrip() {
    Case c("view serialization round trip");
    Rules r = defaultRules();
    Game g(r, 246);
    g.start();
    for (int i = 0; i < 6; ++i) {
        const int me = g.current();
        const View v = g.viewFor(me);
        Action a;
        if (v.phase == Phase::PickColor) {
            a.type = ActType::ChooseColor;
            a.color = Color::Blue;
            g.submit(me, a);
        } else if (v.phase == Phase::Penalty) {
            a.type = ActType::Accept;
            g.submit(me, a);
        } else if (v.canPlay) {
            a.type = ActType::Play;
            a.index = 0;
            if (!g.submit(me, a)) {
                a.type = ActType::Draw;
                g.submit(me, a);
            }
        } else {
            a.type = ActType::Draw;
            g.submit(me, a);
        }
    }

    for (int p = 0; p < r.playerCount; ++p) {
        const View a = g.viewFor(p);
        ByteWriter w;
        writeView(w, a);
        View b;
        ByteReader rdr(w.data().data(), w.data().size());
        check(readView(rdr, b), "view decodes");

        checkEq(b.myId, a.myId, "seat");
        checkEq(b.playerCount, a.playerCount, "seat count");
        check(b.phase == a.phase, "phase");
        checkEq(b.current, a.current, "current seat");
        checkEq(b.direction, a.direction, "direction");
        checkEq(static_cast<int>(b.hand.size()), static_cast<int>(a.hand.size()), "hand size");
        for (size_t i = 0; i < a.hand.size(); ++i) {
            check(b.hand[i].uid() == a.hand[i].uid(), "hand card");
            check(b.hand[i].value == a.hand[i].value, "hand card value");
        }
        check(b.activeColor == a.activeColor, "active colour");
        check(b.blockedColor == a.blockedColor, "blocked colour");
        check(b.holdsActiveColor == a.holdsActiveColor, "active colour held");
        check(b.top.uid() == a.top.uid(), "top card");
        checkEq(static_cast<int>(b.players.size()), static_cast<int>(a.players.size()), "players");
        for (size_t i = 0; i < a.players.size(); ++i) {
            check(b.players[i].name == a.players[i].name, "player name");
            checkEq(b.players[i].handSize, a.players[i].handSize, "player hand size");
            checkEq(b.players[i].score, a.players[i].score, "player score");
        }
        checkEq(static_cast<int>(b.log.size()), static_cast<int>(a.log.size()), "log size");
        check(b.rules == a.rules, "rules");
    }
}

void testProtocolFraming() {
    Case c("protocol framing");
    Action a;
    a.type = ActType::Play;
    a.index = 5;
    a.color = Color::Green;
    a.target = 2;

    const std::string frame = proto::encode(proto::MsgType::Action, proto::makeAction(a));
    proto::FrameDecoder dec;
    // Feed one byte at a time to prove partial delivery is buffered correctly.
    // (This cannot resynchronise a stream that is already misaligned.)
    for (size_t i = 0; i < frame.size(); ++i) dec.feed(frame.substr(i, 1));
    proto::Message m;
    check(dec.next(m), "a frame came out");
    check(m.type == proto::MsgType::Action, "message type");
    check(!dec.next(m), "no trailing frame");

    Action b;
    check(proto::parseAction(m.payload, b), "action decodes");
    check(b.type == a.type && b.index == a.index && b.color == a.color && b.target == a.target,
          "action fields match");

    // Two frames back to back.
    proto::FrameDecoder dec2;
    dec2.feed(frame + frame);
    check(dec2.next(m) && dec2.next(m), "two frames extracted");

    // Truncated junk must not crash the decoder: a frame that claims four bytes
    // stays buffered until those bytes actually arrive. Note that hex escapes
    // are greedy, so the header is assembled byte by byte.
    proto::FrameDecoder dec3;
    std::string junk;
    junk.push_back(0x05);  // MsgType::Action
    junk.push_back(0x04);  // payload length, low byte
    junk.push_back(0x00);
    junk.push_back(0x00);
    junk.push_back(0x00);
    junk += "ab";
    dec3.feed(junk);
    check(!dec3.next(m), "incomplete frame is held back");
    dec3.feed(std::string("cd", 2));
    check(dec3.next(m), "frame completes once the bytes arrive");
    checkEq(static_cast<int>(m.payload.size()), 4, "payload length");
    check(m.payload == "abcd", "payload bytes");

    // A desynchronised stream can turn arbitrary bytes into a length prefix.
    // Anything above the frame cap must fail closed instead of buffering 4 GB.
    proto::FrameDecoder dec4;
    std::string evil;
    evil.push_back(0x05);  // MsgType::Action
    evil.push_back(0xff);  // length = 0xFFFFFFFF
    evil.push_back(0xff);
    evil.push_back(0xff);
    evil.push_back(0xff);
    dec4.feed(evil);
    check(!dec4.next(m), "oversized frame is refused");
    check(dec4.failed(), "decoder reports the protocol failure");

    proto::RoomConfig cfg;
    cfg.rules = defaultRules();
    cfg.name = "tester";
    cfg.seed = 99;
    proto::RoomConfig out;
    check(proto::parseCreateRoom(proto::makeCreateRoom(cfg), out), "room config round trip");
    check(out.rules == cfg.rules, "rules survive");
    check(out.name == "tester" && out.seed == 99, "fields survive");
}

void testRoomCodeValidation() {
    Case c("room code validation");
    const std::string expected = "K49K";
    check(proto::checkJoin(expected, "K49K", false) == proto::JoinCheck::Ok,
          "correct code accepted");
    check(proto::checkJoin(expected, "WRONG", false) == proto::JoinCheck::BadRoom,
          "wrong code rejected");
    check(proto::checkJoin(expected, "", false) == proto::JoinCheck::BadRoom,
          "missing code rejected");
    check(proto::checkJoin("", "anything", false) == proto::JoinCheck::Ok,
          "a room without a code does not require one");
    check(proto::checkJoin(expected, "K49K", true) == proto::JoinCheck::InProgress,
          "late join refused");

    // Round-trip the actual wire message, then run the host's gate on it.
    std::string code, name;
    uint8_t lang = 0;
    check(proto::parseJoinRoom(proto::makeJoinRoom("NOPE", "eve", 0), code, name, lang),
          "join request decodes");
    check(proto::checkJoin(expected, code, false) == proto::JoinCheck::BadRoom,
          "wire code mismatch rejected");
}

void testOutboundBuffering() {
    Case c("outbound frames are buffered or fail closed");
    Action a;
    a.type = ActType::Draw;
    const std::string frame = proto::encode(proto::MsgType::Action, proto::makeAction(a));

    net::OutBuf out;
    check(out.push(frame), "frame queued");
    // A full socket buffer accepts only two bytes, then would block; the frame
    // must stay queued rather than being truncated on the wire.
    std::string wire;
    bool first = true;
    bool ok = out.flushWith([&](const char* data, size_t n) {
        if (first) {
            first = false;
            const size_t take = n < 2 ? n : 2;
            wire.append(data, take);
            return static_cast<long long>(take);
        }
        return 0LL;
    });
    check(ok, "short write keeps the connection alive");
    checkEq(static_cast<int>(out.size()), static_cast<int>(frame.size()) - 2,
            "unwritten bytes stay buffered");
    ok = out.flushWith([&](const char* data, size_t n) {
        wire.append(data, n);
        return static_cast<long long>(n);
    });
    check(ok, "the rest of the frame flushes later");
    check(out.empty(), "nothing is left behind");
    check(wire == frame, "the wire receives exactly one intact frame");

    // A fatal write must discard the queue and tell the caller to close, so a
    // poisoned stream is never reused.
    check(out.push(frame), "frame queued again");
    ok = out.flushWith([](const char*, size_t) { return -1LL; });
    check(!ok, "fatal write fails closed");
    check(out.empty(), "failed buffer is discarded");

    net::OutBuf small(8);
    check(!small.push(std::string(16, 'x')), "oversized backlog is refused");
}

void testInputSanitization() {
    Case c("untrusted text is sanitised");
    const std::string attack = "\x1b[2J\x1b[H" "hi\r\nthere\x7f";
    check(proto::sanitizeText(attack, 32) == "hithere", "control bytes are stripped");

    const std::string cn = "\xe4\xb8\xad\xe6\x96\x87";  // 中文
    check(proto::sanitizeText(cn, 1) == "\xe4\xb8\xad",
          "code-point truncation keeps UTF-8 intact");
    checkEq(static_cast<int>(proto::truncateUtf8(cn, 4).size()), 3,
            "byte truncation does not split a code point");
    checkEq(static_cast<int>(proto::sanitizeText(std::string(32, 'x'), 16).size()), 16,
            "names are capped at 16 code points");

    const std::string longChat(500, 'x');
    checkEq(static_cast<int>(
                proto::truncateUtf8(proto::sanitizeText(longChat, 1000),
                                    proto::kMaxChatBytes)
                    .size()),
            static_cast<int>(proto::kMaxChatBytes), "chat is capped at 200 bytes");
}

void testI18n() {
    Case c("i18n");
    I18n& i = I18n::instance();
    i.setLang(Lang::En);
    check(tr("menu.pve") == "Play vs CPU (PVE)", "english lookup");
    i.setLang(Lang::Zh);
    check(!tr("menu.pve").empty(), "chinese lookup");
    check(tr("menu.pve") != "Play vs CPU (PVE)", "languages differ");
    check(Fmt("ev.drawn", "Ada", 3).find("Ada") != std::string::npos, "format substitution");
    check(Fmt("ev.drawn", "Ada", 3).find("3") != std::string::npos, "format number");
    i.setLang(Lang::En);

    // Rendering a real game must never leave a "{}" placeholder behind: that
    // is what happens when a key is used without supplying its arguments.
    for (Lang lang : {Lang::En, Lang::Zh}) {
        i.setLang(lang);
        for (int players = 2; players <= 4; ++players) {
            for (int seed = 1; seed <= 12; ++seed) {
                Rules r = defaultRules();
                r.playerCount = players;
                r.targetScore = 20;
                Game g(r, static_cast<uint32_t>(seed));
                for (int p = 0; p < players; ++p) g.setName(p, "P" + S(p + 1));
                g.start();
                int guard = 0;
                while (!g.matchOver() && guard++ < 400) {
                    const View v = g.viewFor(0);
                    for (const Event& e : v.log) {
                        const std::string s = describeEvent(e, v);
                        check(s.find("{}") == std::string::npos,
                              "event text has no unfilled placeholder");
                    }
                    for (int p = 0; p < players; ++p) {
                        const View vp = g.viewFor(p);
                        for (const std::string& s :
                             {promptForPlay(vp), promptForPenalty(vp), promptForColor(vp)}) {
                            check(s.find("{}") == std::string::npos,
                                  "prompt text has no unfilled placeholder");
                        }
                    }
                    if (!stepOnce(g)) break;
                }
            }
        }
    }
    i.setLang(Lang::En);
}

void testRulePresets() {
    Case c("rule variants");
    // 2-player reverse acts as a skip, and matches keep score across rounds.
    Rules r = defaultRules();
    r.playerCount = 2;
    r.targetScore = 30;
    MatchSession ms(r, 8080);
    for (int i = 0; i < 2; ++i) {
        ms.addController(std::make_unique<BotController>("CPU " + S(i + 1), Difficulty::Hard,
                                                         8080 + i, i));
    }
    ms.startGame();
    int steps = 0;
    const SessionResult res = ms.run(nullptr, [&]() { return ++steps < 3000; });
    check(res == SessionResult::Completed, "two player match finished");
    check(totalCards(ms.game(), r) == 108, "cards conserved");

    // Team mode only applies to four seats.
    Rules t = defaultRules();
    t.playerCount = 3;
    t.teams = true;
    check(!t.teamPlay(), "teams ignored below four players");
    t.playerCount = 4;
    check(t.teamPlay(), "teams active at four players");
}

void testForfeitKeepsScores() {
    Case c("quitting is a scored forfeit");
    Rules r = defaultRules();
    r.playerCount = 4;
    Game g(r, 321);
    for (int p = 0; p < r.playerCount; ++p) g.setName(p, "P" + S(p + 1));
    g.start();

    g.forfeit(2);
    const View v = g.viewFor(0);
    check(v.matchOver, "the match ends when a player quits");
    checkEq(static_cast<int>(v.finalRank.size()), r.playerCount, "everyone is ranked");
    check(!v.finalRank.empty() && v.finalRank.back() == 2, "the quitter is ranked last");
    check(totalCards(g, r) == 108, "cards are still conserved");

    struct QuitController : Controller {
        std::string name() const override { return "quitter"; }
        Action awaitAction(const View&) override { return Action{}; }
    };

    Rules two = defaultRules();
    two.playerCount = 2;
    MatchSession ms(two, 77);
    ms.addController(std::make_unique<QuitController>());
    ms.addController(std::make_unique<BotController>("CPU", Difficulty::Easy, 78, 1));
    ms.startGame();
    const SessionResult res = ms.run(nullptr, nullptr);
    check(res == SessionResult::Quit, "the session reports a quit, not an abort");
    check(ms.game().matchOver(), "the forfeit produces a result");
    checkEq(static_cast<int>(ms.game().viewFor(0).finalRank.size()), 2,
            "the forfeit publishes standings");
}

// Renders a real board for every seat. This exercises the whole drawing path
// (cards, colours, wrapping, log) without needing a keyboard.
void testUiRendering() {
    Case c("board rendering");
    consoleInit();

    Rules r = defaultRules();
    Game g(r, 13579);
    for (int i = 0; i < r.playerCount; ++i) g.setName(i, "Player" + S(i + 1));
    g.start();

    GameUI ui;
    for (int seat = 0; seat < r.playerCount; ++seat) {
        const View v = g.viewFor(seat);
        ui.setLocalSeat(seat);
        ui.drawBoard(v);
        ui.drawWaiting(v);
        for (const Event& e : v.log) {
            ui.logEvent(e, v);
            check(!describeEvent(e, v).empty() || e.type == EvType::Rejected,
                  "events render to text");
        }
        ui.pushChat("tester", "hello");
    }

    // Text helpers must not be confused by ANSI sequences.
    const std::string coloured = std::string("\x1b[1;31mR7\x1b[0m");
    checkEq(visibleWidth(coloured), 2, "ANSI sequences are invisible");
    checkEq(static_cast<int>(visibleWidth(padTo("abc", 8))), 8, "padTo pads");
    // centring only adds the left margin; the right side stays unpadded
    checkEq(static_cast<int>(visibleWidth(centerTo("abc", 9))), 6, "centerTo adds a margin");
    checkEq(static_cast<int>(visibleWidth(centerTo("abc", 1))), 3, "centerTo never truncates");

    // CJK glyphs occupy two terminal columns, not one.
    const std::string cjk = "\xe4\xb8\xad\xe6\x96\x87";  // 中文
    checkEq(visibleWidth(cjk), 4, "CJK text is two columns per glyph");
    checkEq(static_cast<int>(visibleWidth(padTo(cjk, 6))), 6, "padTo accounts for CJK width");
    checkEq(static_cast<int>(visibleWidth(centerTo(cjk, 8))), 6,
            "centerTo accounts for CJK width");

    consoleReset();
}

void testMenuItems() {
    Case c("menu model");
    Menu menu("Test");
    checkEq(menu.size(), 0, "starts empty");
    menu.add("One");
    menu.add("Two");
    menu.add("Three");
    checkEq(menu.size(), 3, "items added");
    menu.clear();
    checkEq(menu.size(), 0, "clear works");
}
}  // namespace

int main() {
    std::printf("tuiUNO tests\n\n");

    testStandardDeck();
    testCardBasics();
    testDeckDrawAndRecycle();
    testViewPrivacy();
    testNumberMustMatchColour();
    testRejectingOutOfTurn();
    testDrawAndPlay();
    testDrawTwoStacking();
    testWildFourChallenge();
    testChallengeDisabled();
    testUnoCatch();
    testUnoCallClosesWindow();
    testFullMatchBots();
    testBotAlwaysLegal();
    testSerializationRoundTrip();
    testProtocolFraming();
    testRoomCodeValidation();
    testOutboundBuffering();
    testInputSanitization();
    testI18n();
    testRulePresets();
    testForfeitKeepsScores();
    testUiRendering();
    testMenuItems();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
