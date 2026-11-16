#include "uno/game.hpp"

#include <algorithm>

namespace uno {

Game::Game(const Rules& rules, uint32_t seed)
    : rules_(rules), deck_(seed), rng_(seed ^ 0x9e3779b9u) {
    const size_t n = static_cast<size_t>(rules_.playerCount);
    names_.assign(n, "");
    hands_.assign(n, {});
    scores_.assign(n, 0);
    unoSaid_.assign(n, false);
    catchExpired_.assign(n, true);
}

void Game::setName(int seat, const std::string& name) {
    if (seat >= 0 && seat < static_cast<int>(names_.size())) names_[(size_t)seat] = name;
}

// --------------------------------------------------------------------------
// setup
// --------------------------------------------------------------------------

void Game::start() {
    log_.clear();
    rank_.clear();
    matchOver_ = false;
    roundNo_ = 0;
    resetRound();
}

void Game::forfeit(int player) {
    if (matchOver_ || player < 0 || player >= rules_.playerCount) return;
    // Keep the scores as they stand; only the ranking is decided now. The
    // quitter goes last, everyone else is ordered by score (then hand size).
    std::vector<int> order;
    order.reserve(static_cast<size_t>(rules_.playerCount));
    for (int p = 0; p < rules_.playerCount; ++p) {
        if (p != player) order.push_back(p);
    }
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        if (scores_[(size_t)a] != scores_[(size_t)b]) {
            return scores_[(size_t)a] > scores_[(size_t)b];
        }
        return hands_[(size_t)a].size() < hands_[(size_t)b].size();
    });
    order.push_back(player);
    rank_ = order;
    matchOver_ = true;
    phase_ = Phase::Over;
    current_ = -1;
}

void Game::resetRound() {
    ++roundNo_;
    deck_.reset();
    played_.clear();
    winners_.clear();
    const size_t n = names_.size();
    unoSaid_.assign(n, false);
    catchExpired_.assign(n, true);
    for (auto& h : hands_) h.clear();
    pendingDraw_ = 0;
    pendingIndex_ = -1;
    challengeOpen_ = false;
    openingWildFour_ = false;
    drewCard_ = false;
    hasLastDrawn_ = false;
    direction_ = 1;
    turnNo_ = 0;
    activeColor_ = Color::None;
    current_ = static_cast<int>(rng_() % static_cast<uint32_t>(n));

    deal();
    openCard();
    log_.clear();
    emit(EvType::Started);
    applyOpeningCard();
}

void Game::deal() {
    for (int p = 0; p < rules_.playerCount; ++p) takeCards(p, rules_.initialCards);
    if (rules_.swapHands && rules_.initialCards >= 7) {
        const int hops = rules_.initialCards / 7;
        for (int p = 0; p < rules_.playerCount; ++p) {
            int q = nextSeat(p, hops);
            if (q != p) std::swap(hands_[(size_t)p], hands_[(size_t)q]);
        }
        emit(EvType::HandSwapped);
    }
}

void Game::openCard() {
    Card first;
    if (!deck_.tryDrawOne(first)) return;
    deck_.discard(first);
    played_.push_back(PlayedCard{first, first.color});
    activeColor_ = first.color;
}

// The flipped card can immediately affect the opening turn.
void Game::applyOpeningCard() {
    const Card first = played_.back().card;

    switch (first.kind) {
        case Kind::Number:
            break;
        case Kind::Skip:
            emit(EvType::Skipped, current_);
            current_ = nextSeat(current_, 2);
            break;
        case Kind::Reverse:
            emit(EvType::Reversed);
            direction_ = -direction_;
            if (rules_.playerCount == 2) current_ = nextSeat(current_, 2);
            break;
        case Kind::DrawTwo: {
            int victim = current_;
            emit(EvType::Penalty, victim, 2);
            takeCards(victim, 2);
            current_ = nextSeat(current_, 2);
            break;
        }
        case Kind::Wild:
            // Starter declares the opening colour.
            activeColor_ = Color::None;
            turnNo_ = 1;
            phase_ = Phase::PickColor;
            emit(EvType::Turn, current_);
            return;
        case Kind::WildDrawFour:
            // Starter eats four cards and is skipped; the following seat names
            // the colour so the round is not decided by luck.
            emit(EvType::Penalty, current_, 4);
            takeCards(current_, 4);
            activeColor_ = Color::None;
            openingWildFour_ = true;
            current_ = nextSeat(current_, 2);
            turnNo_ = 1;
            phase_ = Phase::PickColor;
            emit(EvType::Turn, current_);
            return;
    }
    beginTurn();
}

// --------------------------------------------------------------------------
// helpers
// --------------------------------------------------------------------------

int Game::nextSeat(int from, int steps) const {
    const int n = rules_.playerCount;
    const int dir = (steps < 0) ? -1 : 1;
    int p = from;
    for (int i = 0; i < (steps < 0 ? -steps : steps); ++i) {
        p += dir * direction_;
        if (p >= n) p = 0;
        if (p < 0) p = n - 1;
    }
    return p;
}

void Game::emit(EvType t, int player, int value, Color color, const Card& card) {
    Event e;
    e.type = t;
    e.player = player;
    e.value = value;
    e.color = color;
    e.card = card;
    log_.push_back(e);
}

void Game::takeCards(int player, int n) {
    if (n <= 0) return;
    std::vector<Card> got = deck_.draw(n);
    hands_[(size_t)player].insert(hands_[(size_t)player].end(), got.begin(), got.end());
    emit(EvType::Drawn, player, n);
}

bool Game::hasLegalPlay(int player) const {
    const auto& h = hands_[(size_t)player];
    for (size_t i = 0; i < h.size(); ++i) {
        if (canPlayIndex(player, static_cast<int>(i))) return true;
    }
    return false;
}

bool Game::canPlayIndex(int player, int index) const {
    const auto& h = hands_[(size_t)player];
    if (index < 0 || index >= static_cast<int>(h.size())) return false;
    const Card& c = h[(size_t)index];
    // A Wild Draw Four is always playable. Playing it while holding a card of
    // the colour in effect is a cheat, but like the physical game it is only
    // punished when somebody challenges and wins.
    return c.playableOn(activeColor_);
}

bool Game::canStackFor(int player) const {
    (void)player;
    if (pendingIndex_ < 0 || pendingIndex_ >= static_cast<int>(played_.size())) return false;
    const Card& pc = played_[(size_t)pendingIndex_].card;
    if (pc.kind == Kind::DrawTwo) return rules_.stackDrawTwo;
    if (pc.kind == Kind::WildDrawFour) return rules_.wildFourStack;
    return false;
}

int Game::catchable(int actor) const {
    if (!rules_.unoPenalty) return -1;
    for (int p = 0; p < rules_.playerCount; ++p) {
        if (p == actor) continue;
        if (hands_[(size_t)p].size() == 1 && !unoSaid_[(size_t)p] &&
            !catchExpired_[(size_t)p]) {
            return p;
        }
    }
    return -1;
}

// --------------------------------------------------------------------------
// turn flow
// --------------------------------------------------------------------------

void Game::beginTurn() {
    ++turnNo_;
    drewCard_ = false;
    hasLastDrawn_ = false;

    // The catch window opens when an opponent drops to one card without saying
    // UNO, and closes only once that player's own turn comes back around.
    if (hands_[(size_t)current_].size() == 1 && !unoSaid_[(size_t)current_]) {
        catchExpired_[(size_t)current_] = true;
    }

    phase_ = (pendingDraw_ > 0) ? Phase::Penalty : Phase::Play;
    emit(EvType::Turn, current_);
}

// --------------------------------------------------------------------------
// actions
// --------------------------------------------------------------------------

bool Game::submit(int player, const Action& action) {
    if (phase_ == Phase::Over || phase_ == Phase::Setup) return false;
    if (player < 0 || player >= rules_.playerCount) return false;

    // Catching a missing UNO is a side action: it costs the offender two cards
    // and does not consume the current player's turn.
    if (action.type == ActType::Catch) {
        const int target = catchable(player);
        if (target < 0 || action.target != target) {
            emit(EvType::Rejected, player);
            return false;
        }
        emit(EvType::UnoCaught, player, target);
        emit(EvType::Penalty, target, 2);
        takeCards(target, 2);
        return true;
    }

    if (player != current_) {
        emit(EvType::Rejected, player);
        return false;
    }

    if (action.type == ActType::CallUno) {
        if (hands_[(size_t)player].size() > 2) {
            emit(EvType::Rejected, player);
            return false;
        }
        unoSaid_[(size_t)player] = true;
        emit(EvType::UnoSaid, player);
        return true;
    }

    switch (phase_) {
        case Phase::PickColor: {
            if (action.type != ActType::ChooseColor) break;
            const Color c = action.color;
            if (c != Color::Red && c != Color::Yellow && c != Color::Green &&
                c != Color::Blue) {
                break;
            }
            const Card wild = played_.back().card;
            played_.back().color = c;
            activeColor_ = c;
            emit(EvType::ColorChosen, player, 0, c);
            if (wild.kind == Kind::WildDrawFour && !openingWildFour_) {
                applyWildEffect(player, false);
            } else {
                openingWildFour_ = false;
                current_ = nextSeat(player, 1);
                beginTurn();
            }
            return true;
        }

        case Phase::Penalty: {
            if (action.type == ActType::Challenge && challengeOpen_) {
                resolvePenalty(player, true);
                return true;
            }
            if (action.type == ActType::Accept) {
                resolvePenalty(player, false);
                return true;
            }
            if (action.type == ActType::Play && canStackFor(player)) {
                if (canPlayIndex(player, action.index)) {
                    applyPlay(player, action.index);
                    return true;
                }
            }
            break;
        }

        case Phase::Play: {
            if (action.type == ActType::Play) {
                if (!canPlayIndex(player, action.index)) break;
                applyPlay(player, action.index);
                return true;
            }
            if (action.type == ActType::Draw) {
                if (drewCard_) break;  // one draw per turn
                if (rules_.mustPlay && hasLegalPlay(player)) break;
                if (deck_.drawCount() == 0 && rules_.recycleDiscard) {
                    if (deck_.reshuffleDiscard()) emit(EvType::Recycle);
                }
                drewCard_ = true;
                if (!deck_.tryDrawOne(lastDrawn_)) {
                    // Nothing left to draw and nothing to recycle: the turn
                    // simply passes.
                    hasLastDrawn_ = false;
                    current_ = nextSeat(player, 1);
                    pendingDraw_ = 0;
                    pendingIndex_ = -1;
                    challengeOpen_ = false;
                    beginTurn();
                    return true;
                }
                hasLastDrawn_ = true;
                hands_[(size_t)player].push_back(lastDrawn_);
                emit(EvType::Drawn, player, 1);
                if (!hasLegalPlay(player)) {
                    // Still nothing to play: pass automatically.
                    current_ = nextSeat(player, 1);
                    pendingDraw_ = 0;
                    pendingIndex_ = -1;
                    challengeOpen_ = false;
                    beginTurn();
                }
                return true;
            }
            if (action.type == ActType::Pass && drewCard_) {
                current_ = nextSeat(player, 1);
                pendingDraw_ = 0;
                pendingIndex_ = -1;
                challengeOpen_ = false;
                beginTurn();
                return true;
            }
            break;
        }

        default:
            break;
    }

    emit(EvType::Rejected, player);
    return false;
}

// Plays the card and resolves everything except a wild's turn effect.
void Game::applyPlay(int player, int index) {
    auto& h = hands_[(size_t)player];
    const Card c = h[(size_t)index];
    h.erase(h.begin() + index);

    deck_.discard(c);
    played_.push_back(PlayedCard{c, c.color});
    const int myIndex = static_cast<int>(played_.size()) - 1;
    emit(EvType::Played, player, 0, Color::None, c);

    if (h.size() == 1) {
        catchExpired_[(size_t)player] = false;  // catchable until their next turn
    } else {
        unoSaid_[(size_t)player] = false;
        catchExpired_[(size_t)player] = true;
    }

    drewCard_ = false;
    hasLastDrawn_ = false;

    int skip = 1;

    switch (c.kind) {
        case Kind::Number:
            activeColor_ = c.color;
            break;
        case Kind::Skip:
            activeColor_ = c.color;
            emit(EvType::Skipped, player);
            skip = 2;
            break;
        case Kind::Reverse:
            activeColor_ = c.color;
            emit(EvType::Reversed);
            if (rules_.playerCount == 2) {
                emit(EvType::Skipped, player);
                skip = 2;
            }
            break;
        case Kind::DrawTwo:
            activeColor_ = c.color;
            if (rules_.stackDrawTwo) {
                pendingDraw_ += 2;
                pendingIndex_ = myIndex;
                phase_ = Phase::Penalty;
                emit(EvType::Stacked, player, pendingDraw_);
            } else {
                const int victim = nextSeat(player, 1);
                emit(EvType::Penalty, victim, 2);
                takeCards(victim, 2);
                skip = 2;
            }
            break;
        case Kind::Wild:
            activeColor_ = Color::None;
            phase_ = Phase::PickColor;
            checkRoundEnd(player);
            return;
        case Kind::WildDrawFour:
            colorBeforeWildFour_ = activeColor_;
            activeColor_ = Color::None;
            phase_ = Phase::PickColor;
            checkRoundEnd(player);
            return;
    }

    checkRoundEnd(player);
    if (phase_ == Phase::Over) return;

    current_ = nextSeat(player, skip);
    beginTurn();
}

// Second half of a Wild Draw Four: the colour is known now, so the penalty can
// be created (or applied immediately when challenge/stacking are disabled).
void Game::applyWildEffect(int player, bool opening) {
    if (rules_.wildFourChallenge || rules_.wildFourStack) {
        pendingDraw_ += 4;
        pendingIndex_ = static_cast<int>(played_.size()) - 1;
        challengeOpen_ = (rules_.wildFourChallenge && pendingDraw_ == 4);
        phase_ = Phase::Penalty;
        emit(EvType::Stacked, player, pendingDraw_);
        current_ = nextSeat(player, 1);
        beginTurn();
        return;
    }
    const int victim = opening ? player : nextSeat(player, 1);
    emit(EvType::Penalty, victim, 4);
    takeCards(victim, 4);
    if (opening) return;
    current_ = nextSeat(player, 2);
    beginTurn();
}

void Game::checkRoundEnd(int player) {
    if (hands_[(size_t)player].empty()) settleRound(player);
}

void Game::resolvePenalty(int player, bool challenged) {
    const int attacker = (pendingIndex_ >= 0) ? nextSeat(player, -1) : -1;

    if (challenged) {
        // The challenger asserts the Wild Draw Four was illegal. Verify against
        // the attacker's hand; if they held the blocked colour it was legal.
        bool legal = true;
        if (attacker >= 0) {
            for (const Card& x : hands_[(size_t)attacker]) {
                if (x.color != Color::None && x.color == colorBeforeWildFour_) {
                    legal = false;
                    break;
                }
            }
        }
        if (!legal) {
            emit(EvType::ChallengeOK, player, attacker);
            if (pendingIndex_ >= 0 && pendingIndex_ < static_cast<int>(played_.size())) {
                played_.erase(played_.begin() + pendingIndex_);
            }
            activeColor_ = colorBeforeWildFour_;
            emit(EvType::Penalty, attacker, 4);
            takeCards(attacker, 4);
            emit(EvType::Penalty, player, 2);
            takeCards(player, 2);
            pendingDraw_ = 0;
            pendingIndex_ = -1;
            challengeOpen_ = false;
            phase_ = Phase::Play;
            drewCard_ = false;
            hasLastDrawn_ = false;
            current_ = player;
            beginTurn();
            return;
        }
        emit(EvType::ChallengeBad, player, pendingDraw_);
    }

    emit(EvType::Penalty, player, pendingDraw_);
    takeCards(player, pendingDraw_);
    pendingDraw_ = 0;
    pendingIndex_ = -1;
    challengeOpen_ = false;
    current_ = nextSeat(player, 1);
    beginTurn();
}

void Game::settleRound(int winner) {
    winners_.push_back(winner);
    emit(EvType::PlayerOut, winner);

    if (rules_.teamPlay()) {
        const int winnersTeam = winner % 2;
        int gained = 0;
        for (int p = 0; p < rules_.playerCount; ++p) {
            if (p % 2 == winnersTeam) continue;
            for (const Card& c : hands_[(size_t)p]) gained += c.points();
        }
        scores_[(size_t)winner] += gained;
        emit(EvType::Penalty, winner, gained);
    } else {
        for (int p = 0; p < rules_.playerCount; ++p) {
            if (p == winner) continue;
            int sum = 0;
            for (const Card& c : hands_[(size_t)p]) sum += c.points();
            scores_[(size_t)p] += sum;
        }
    }
    emit(EvType::RoundOver, winner);

    std::vector<int> order;
    order.reserve(names_.size());
    for (size_t p = 0; p < names_.size(); ++p) order.push_back(static_cast<int>(p));
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        if (scores_[(size_t)a] != scores_[(size_t)b]) return scores_[(size_t)a] > scores_[(size_t)b];
        return hands_[(size_t)a].size() < hands_[(size_t)b].size();
    });
    rank_ = order;

    int champ = -1;
    for (int p : order) {
        if (scores_[(size_t)p] >= rules_.targetScore) {
            champ = p;
            break;
        }
    }
    if (champ < 0) {
        resetRound();
        return;
    }

    matchOver_ = true;
    phase_ = Phase::Over;
    emit(EvType::MatchOver, champ);
}

// --------------------------------------------------------------------------
// views
// --------------------------------------------------------------------------

View Game::viewFor(int player) const {
    View v;
    v.myId = player;
    v.playerCount = rules_.playerCount;
    v.phase = phase_;
    v.current = current_;
    v.direction = direction_;
    v.turnNo = turnNo_;
    v.roundNo = roundNo_;
    v.matchOver = matchOver_;
    v.rules = rules_;
    v.activeColor = activeColor_;
    v.log = log_;
    v.winners = winners_;
    v.finalRank = matchOver_ ? rank_ : std::vector<int>();
    v.drawCount = deck_.drawCount();
    v.discardCount = deck_.discardCount();

    // The effective top card comes from the played history, not the deck: after
    // a successful Wild+4 challenge the deck still holds the returned card.
    if (!played_.empty()) v.top = played_.back().card;
    if (pendingIndex_ >= 0 && pendingIndex_ < static_cast<int>(played_.size())) {
        v.pendingCard = played_[(size_t)pendingIndex_].card;
        v.claimedColor = played_[(size_t)pendingIndex_].color;
        if (v.pendingCard.kind == Kind::WildDrawFour) v.blockedColor = colorBeforeWildFour_;
    }
    v.pendingDraw = pendingDraw_;

    for (size_t p = 0; p < names_.size(); ++p) {
        PlayerInfo pi;
        pi.id = static_cast<int>(p);
        pi.name = names_[p];
        pi.handSize = static_cast<int>(hands_[p].size());
        pi.score = scores_[p];
        pi.unoCalled = unoSaid_[p] && pi.handSize <= 1;
        pi.isBot = pi.name.rfind("CPU", 0) == 0;
        v.players.push_back(pi);
    }

    if (player >= 0 && player < static_cast<int>(hands_.size())) {
        v.hand = hands_[(size_t)player];
        if (hasLastDrawn_) v.justDrawn = lastDrawn_;
        v.hasJustDrawn = hasLastDrawn_ && current_ == player;

        const bool mine = (player == current_);
        v.canPlay = mine && hasLegalPlay(player);
        v.canStack = mine && phase_ == Phase::Penalty && canStackFor(player);
        v.canPass = mine && phase_ == Phase::Play && drewCard_;
        v.canDraw = mine && phase_ == Phase::Play && !drewCard_ &&
                    !(rules_.mustPlay && hasLegalPlay(player));
        v.callUnoAvailable = mine && hands_[(size_t)player].size() <= 2 &&
                             !unoSaid_[(size_t)player];
        v.challengeOpen = mine && phase_ == Phase::Penalty && challengeOpen_;
        v.catchTarget = mine ? catchable(player) : -1;

        bool haveActive = false;
        for (const Card& c : v.hand) {
            if (c.color != Color::None && c.color == activeColor_) haveActive = true;
        }
        v.holdsActiveColor = (activeColor_ != Color::None) && haveActive;
    }
    return v;
}

}  // namespace uno