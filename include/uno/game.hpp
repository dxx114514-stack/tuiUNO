// Authoritative UNO rules engine. Pure logic: no I/O, no rendering. It accepts
// actions and produces a View that a local UI, an AI or a remote client can
// render identically.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "uno/card.hpp"
#include "uno/deck.hpp"
#include "uno/rules.hpp"

namespace uno {

enum class Phase : uint8_t {
    Setup = 0,      // not started yet
    Play = 1,       // current player may draw or play
    PickColor = 2,  // a wild was played, colour still unresolved
    Penalty = 3,    // a pending draw-two / wild+4 must be resolved
    Over = 4,       // match finished
};

enum class ActType : uint8_t {
    None = 0,
    Draw = 1,
    Play = 2,
    Pass = 3,          // decline the card just drawn
    ChooseColor = 4,
    Challenge = 5,     // the pending Wild+4 was illegal
    Accept = 6,        // take the pending penalty
    CallUno = 7,
    Catch = 8,         // catch an opponent on one card who never said UNO
};

struct Action {
    ActType type = ActType::None;
    int index = -1;             // hand index, for ActType::Play
    Color color = Color::None;  // for ActType::ChooseColor
    int target = -1;            // for ActType::Catch
};

enum class EvType : uint8_t {
    Started, Turn, Drawn, Recycle, Played, ColorChosen, Skipped, Reversed,
    Penalty, Stacked, ChallengeOK, ChallengeBad, UnoSaid, UnoCaught,
    HandSwapped, PlayerOut, RoundOver, MatchOver, Rejected, Left, Joined,
    Disconnected,
};

struct Event {
    EvType type = EvType::Started;
    int player = -1;
    int value = 0;             // secondary number (card count, seat, ...)
    Color color = Color::None;
    Card card;
    std::vector<Card> cards;   // cards revealed by this event
    std::string text;          // already-localised fallback text
};

struct PlayerInfo {
    int id = 0;
    std::string name;
    int handSize = 0;
    int score = 0;
    bool unoCalled = false;
    bool connected = true;
    bool isBot = false;
};

// Per-player projection of the game state. `hand` only ever contains that
// participant's own cards, so a snapshot is safe to send over the wire.
struct View {
    int myId = 0;
    int playerCount = 0;
    Phase phase = Phase::Setup;
    int current = 0;
    int direction = 1;
    int turnNo = 0;
    int roundNo = 1;
    bool matchOver = false;

    Card top;                          // face-up card in the middle
    Color activeColor = Color::None;   // colour currently in effect
    Color claimedColor = Color::None;  // colour the pending Wild+4 claimed

    std::vector<PlayerInfo> players;
    std::vector<Card> hand;            // local player's hand only
    Card justDrawn;
    bool hasJustDrawn = false;

    int pendingDraw = 0;               // penalty waiting to be resolved
    Card pendingCard;
    Color blockedColor = Color::None;  // colour in effect before the pending Wild+4
    bool challengeOpen = false;
    bool canStack = false;             // may stack onto the pending card
    bool canPass = false;              // may decline the card just drew
    bool canDraw = false;
    bool canPlay = false;              // has at least one legal card
    bool holdsActiveColor = false;     // a Wild+4 played now would be a cheat
    bool callUnoAvailable = false;

    int catchTarget = -1;              // opponent eligible to be caught
    int drawCount = 0;
    int discardCount = 0;

    std::vector<int> winners;          // seats that went out this round
    std::vector<int> finalRank;        // match standings, best first
    Rules rules;
    std::vector<Event> log;
};

class Game {
  public:
    Game(const Rules& rules, uint32_t seed);

    void setName(int seat, const std::string& name);
    void start();
    // Ends the match because `player` left: current scores are kept and the
    // quitter is placed last in the final standings.
    void forfeit(int player);

    // Applies an action on behalf of `player`. Returns false and appends a
    // Rejected event when the action is illegal; state stays untouched.
    bool submit(int player, const Action& action);

    Phase phase() const { return phase_; }
    int current() const { return current_; }
    int turnNo() const { return turnNo_; }
    int roundNo() const { return roundNo_; }
    bool matchOver() const { return matchOver_; }
    const Rules& rules() const { return rules_; }

    View viewFor(int player) const;
    const std::vector<Card>& handOf(int player) const { return hands_[(size_t)player]; }
    const Deck& deck() const { return deck_; }
    Color activeColor() const { return activeColor_; }

  private:
    struct PlayedCard {
        Card card;
        Color color;   // colour this card put into effect
    };

    void resetRound();
    void deal();
    void openCard();
    void applyOpeningCard();
    void beginTurn();
    int nextSeat(int from, int steps) const;
    void emit(EvType t, int player = -1, int value = 0, Color color = Color::None,
              const Card& card = Card());
    void takeCards(int player, int n);

    bool hasLegalPlay(int player) const;
    bool canPlayIndex(int player, int index) const;
    bool canStackFor(int player) const;
    void applyPlay(int player, int index);
    void applyWildEffect(int player, bool opening);
    void resolvePenalty(int player, bool challenged);
    void checkRoundEnd(int player);
    void settleRound(int winner);
    int catchable(int actor) const;

    Rules rules_;
    Deck deck_;
    std::mt19937 rng_;
    std::vector<std::string> names_;
    std::vector<std::vector<Card>> hands_;
    std::vector<int> scores_;
    std::vector<bool> unoSaid_;
    std::vector<bool> catchExpired_;
    std::vector<PlayedCard> played_;
    std::vector<Event> log_;

    Phase phase_ = Phase::Setup;
    int current_ = 0;
    int direction_ = 1;
    int turnNo_ = 0;
    int roundNo_ = 0;
    Color activeColor_ = Color::None;

    bool drewCard_ = false;
    Card lastDrawn_;
    bool hasLastDrawn_ = false;
    bool openingWildFour_ = false;  // the flipped card was a Wild Draw Four

    int pendingDraw_ = 0;
    int pendingIndex_ = -1;      // index into played_ of the offending card
    bool challengeOpen_ = false;
    Color colorBeforeWildFour_ = Color::None;

    std::vector<int> winners_;
    std::vector<int> rank_;
    bool matchOver_ = false;
};

}  // namespace uno