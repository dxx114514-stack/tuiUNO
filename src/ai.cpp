#include "uno/ai.hpp"

#include <algorithm>

#include "uno/deck.hpp"
#include "uno/i18n.hpp"

namespace uno {

Difficulty difficultyFromInt(int v) {
    if (v <= 0) return Difficulty::Easy;
    if (v == 1) return Difficulty::Normal;
    return Difficulty::Hard;
}

const char* difficultyName(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return "Easy";
        case Difficulty::Normal: return "Normal";
        case Difficulty::Hard: return "Hard";
    }
    return "Normal";
}

std::string difficultyLabel(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return tr("pve.diff0");
        case Difficulty::Normal: return tr("pve.diff1");
        case Difficulty::Hard: return tr("pve.diff2");
    }
    return tr("pve.diff1");
}

namespace {

const Color kAllColors[kNumColors] = {Color::Red, Color::Yellow, Color::Green, Color::Blue};

int at(const std::map<uint32_t, int>& m, uint32_t key) {
    auto it = m.find(key);
    return it == m.end() ? 0 : it->second;
}

// uids encode the colour in a 20-wide block; see Card::uid().
uint32_t colorBlock(uint32_t uid) { return uid / 20u; }

}  // namespace

Bot::Bot(Difficulty diff, uint32_t seed)
    : diff_(diff), rng_(seed ^ (0x2545f491u * (seed + 0x9e3779b9u))) {
    for (const Card& c : standardDeck()) total_[c.uid()]++;
}

// ---------------------------------------------------------------------------
// randomness
// ---------------------------------------------------------------------------

int Bot::rnd(int lo, int hi) {
    if (hi <= lo) return lo;
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng_);
}

double Bot::rnd01() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng_);
}

bool Bot::chance(double p) { return rnd01() < p; }

// ---------------------------------------------------------------------------
// deck accounting
//
// total_  : every card in a fresh 108-card deck
// hand_   : the cards this bot is holding right now
// seen_   : cards face up on the table since the last recycle
// pool    : total_ - hand_ - seen_  == everything unaccounted for, i.e. the
//           opponents' hands plus the draw pile.
// ---------------------------------------------------------------------------

void Bot::rebuildPool(const View& v) {
    hand_.clear();
    for (const Card& c : v.hand) hand_[c.uid()]++;
}

int Bot::poolCount() const {
    int n = 0;
    for (const auto& kv : total_) {
        const int left = kv.second - at(hand_, kv.first) - at(seen_, kv.first);
        if (left > 0) n += left;
    }
    return n;
}

int Bot::poolCount(Color c) const {
    if (c == Color::None) return 0;
    int n = 0;
    for (const auto& kv : total_) {
        if (colorBlock(kv.first) != static_cast<uint32_t>(c)) continue;
        const int left = kv.second - at(hand_, kv.first) - at(seen_, kv.first);
        if (left > 0) n += left;
    }
    return n;
}

int Bot::poolCount(Kind k) const {
    int n = 0;
    const uint32_t wild = Card::wild().uid();
    const uint32_t wildFour = Card::wildFour().uid();
    for (const auto& kv : total_) {
        const uint32_t uid = kv.first;
        bool match = false;
        if (uid == wild) {
            match = (k == Kind::Wild);
        } else if (uid == wildFour) {
            match = (k == Kind::WildDrawFour);
        } else {
            const int offset = static_cast<int>(uid % 20u);
            Kind kind = Kind::Number;
            if (offset > 9 && offset < 12) continue;
            if (offset == 12) kind = Kind::Skip;
            else if (offset == 13) kind = Kind::Reverse;
            else if (offset == 14) kind = Kind::DrawTwo;
            if (kind == k) match = true;
        }
        if (!match) continue;
        const int left = kv.second - at(hand_, uid) - at(seen_, uid);
        if (left > 0) n += left;
    }
    return n;
}

double Bot::density(Color c) const {
    return static_cast<double>(poolCount(c)) / std::max(1, poolCount());
}

double Bot::chanceOpponentHas(Kind k, int /*opponent*/) const {
    return static_cast<double>(poolCount(k)) / std::max(1, poolCount());
}

double Bot::expectedIn(int handSize, Color c) const {
    if (handSize <= 0 || c == Color::None) return 0.0;
    const int total = poolCount();
    if (total <= 0) return 0.0;
    return static_cast<double>(handSize) * static_cast<double>(poolCount(c)) /
           static_cast<double>(total);
}

int Bot::handColorCount(Color c) const {
    int n = 0;
    for (const auto& kv : hand_) {
        if (colorBlock(kv.first) == static_cast<uint32_t>(c)) n += kv.second;
    }
    return n;
}

// ---------------------------------------------------------------------------
// observation
// ---------------------------------------------------------------------------

void Bot::observe(const View& v) {
    int seenTotal = 0;
    for (const auto& kv : seen_) seenTotal += kv.second;

    // The discard pile collapsed back to a single card: it was recycled, so
    // everything except the new top is unknown again.
    if (v.discardCount <= 1 && seenTotal > 1) {
        seen_.clear();
        seen_[v.top.uid()]++;
        seenTotal = 1;
    }

    for (const Event& e : v.log) {
        if (e.type == EvType::Played) {
            seen_[e.card.uid()]++;
        }
    }

    started_ = true;
    rebuildPool(v);
}

// ---------------------------------------------------------------------------
// decision entry point
// ---------------------------------------------------------------------------

Action Bot::decide(const View& v) {
    Action none;
    if (v.phase == Phase::Over) return none;

    // Catch an opponent caught short of saying UNO.
    if (v.catchTarget >= 0) {
        Action a;
        a.type = ActType::Catch;
        a.target = v.catchTarget;
        return a;
    }

    // Shout UNO as soon as we are down to two cards. Easy bots sometimes
    // forget on purpose, which simply means they play on without saying it.
    if (v.callUnoAvailable) {
        const bool forget = (diff_ == Difficulty::Easy) && chance(0.45);
        if (!forget) {
            Action a;
            a.type = ActType::CallUno;
            return a;
        }
    }

    switch (v.phase) {
        case Phase::PickColor:
            return decidePickColor(v);
        case Phase::Penalty:
            return decidePenalty(v);
        case Phase::Play:
            return decidePlay(v);
        default:
            break;
    }

    // Never hand back an empty action: a controller that returns None is
    // treated as a forfeit by the match loop.
    Action a;
    a.type = v.canDraw ? ActType::Draw : ActType::Pass;
    return a;
}

// ---------------------------------------------------------------------------
// penalty resolution
// ---------------------------------------------------------------------------

Action Bot::decidePenalty(const View& v) {
    Action a;

    if (v.challengeOpen) {
        // The Wild+4 was a cheat if the attacker already held a card of the
        // colour that was in effect. Estimate that from the untracked pool:
        // the more of that colour is floating around, the likelier it is.
        const int attacker = (v.current + v.playerCount - 1) % v.playerCount;
        const int attackerSize =
            (attacker < static_cast<int>(v.players.size())) ? v.players[(size_t)attacker].handSize
                                                            : 4;
        const double suspicion = expectedIn(attackerSize, v.blockedColor);

        // A Wild+4 also has to be physically plausible: with all four spent,
        // the attacker must have been holding one.
        const bool w4Exists = poolCount(Kind::WildDrawFour) > 0;

        double threshold = -1.0;
        switch (diff_) {
            case Difficulty::Easy: threshold = -1.0; break;  // never challenges
            case Difficulty::Normal: threshold = 0.50; break;
            case Difficulty::Hard: threshold = 0.32; break;
        }
        if (threshold > 0.0 && w4Exists && suspicion >= threshold) {
            a.type = ActType::Challenge;
            return a;
        }
    }

    if (v.canStack) {
        // Pass the penalty along when the next player is unlikely to counter it.
        int best = -1;
        for (size_t i = 0; i < v.hand.size(); ++i) {
            const Card& c = v.hand[i];
            if (c.kind == v.pendingCard.kind && c.playableOn(v.activeColor)) best = static_cast<int>(i);
        }
        if (best >= 0) {
            const double counter = chanceOpponentHas(v.pendingCard.kind, v.current);
            const bool stackIt =
                (diff_ == Difficulty::Hard) ? (counter < 0.50) : (counter < 0.22);
            if (stackIt) {
                a.type = ActType::Play;
                a.index = best;
                return a;
            }
        }
    }

    a.type = ActType::Accept;
    return a;
}

// ---------------------------------------------------------------------------
// colour selection
// ---------------------------------------------------------------------------

Color Bot::chooseColor(const View& v, int /*seat*/) {
    if (diff_ == Difficulty::Easy) {
        // Still random, but avoid naming a colour we hold nothing of: that
        // simply hands ourselves a dead turn and stalls the round.
        std::vector<Color> usable;
        for (Color c : kAllColors) {
            if (handColorCount(c) > 0) usable.push_back(c);
        }
        if (usable.empty()) return kAllColors[rnd(0, kNumColors - 1)];
        return usable[(size_t)rnd(0, static_cast<int>(usable.size()) - 1)];
    }

    const int nx = (v.current + v.direction + v.playerCount) % v.playerCount;
    const int nxSize = (nx < static_cast<int>(v.players.size())) ? v.players[(size_t)nx].handSize : 5;

    double best = -1e18;
    Color bestColor = kAllColors[0];
    for (Color c : kAllColors) {
        // Fewer untracked cards of this colour means the next player is less
        // likely to be able to continue. Clamped against huge hands for the
        // same reason as in bestPlayIndex().
        const int weight = std::min(nxSize, 12);
        double score = -density(c) * 14.0 * static_cast<double>(weight);
        // A player who cannot move hoards cards and the round never ends, so
        // avoid handing them a dead turn unless we are out of options.
        if (expectedIn(nxSize, c) < 0.30) score -= 40.0;
        // Holding a pile of a colour is an asset: we can unload it later.
        score += static_cast<double>(handColorCount(c)) * 12.0;
        // Never paint ourselves into a corner near the end of the deck.
        if (handColorCount(c) == 0 && v.drawCount < 12) score -= 30.0;
        if (score > best) {
            best = score;
            bestColor = c;
        }
    }
    if (diff_ == Difficulty::Normal && chance(0.12)) return kAllColors[rnd(0, kNumColors - 1)];
    return bestColor;
}

Action Bot::decidePickColor(const View& v) {
    Action a;
    a.type = ActType::ChooseColor;
    a.color = chooseColor(v, v.current);
    return a;
}

// ---------------------------------------------------------------------------
// normal turn
// ---------------------------------------------------------------------------

Action Bot::decidePlay(const View& v) {
    if (!v.canPlay) {
        Action a;
        a.type = ActType::Draw;
        if (!v.canDraw) a.type = ActType::Pass;
        return a;
    }

    if (diff_ == Difficulty::Easy) {
        if (chance(0.15)) {
            Action a;
            a.type = v.canDraw ? ActType::Draw : ActType::Pass;
            return a;
        }
        std::vector<int> legal;
        for (size_t i = 0; i < v.hand.size(); ++i) {
            const Card& c = v.hand[i];
            if (!c.playableOn(v.activeColor)) continue;
            if (c.kind == Kind::WildDrawFour && v.holdsActiveColor) continue;
            legal.push_back(static_cast<int>(i));
        }
        Action a;
        if (legal.empty()) {
            a.type = v.canDraw ? ActType::Draw : ActType::Pass;
        } else {
            a.type = ActType::Play;
            a.index = legal[(size_t)rnd(0, static_cast<int>(legal.size()) - 1)];
        }
        return a;
    }

    const int pick = bestPlayIndex(v);
    Action a;
    if (pick < 0) {
        a.type = v.canDraw ? ActType::Draw : ActType::Pass;
        return a;
    }
    a.type = ActType::Play;
    a.index = pick;
    return a;
}

int Bot::bestPlayIndex(const View& v) {
    const int n = static_cast<int>(v.hand.size());
    if (n == 0) return -1;

    const int nx = (v.current + v.direction + v.playerCount) % v.playerCount;
    const int nxSize = (nx < static_cast<int>(v.players.size())) ? v.players[(size_t)nx].handSize : 5;
    const int nx2 = (nx + v.direction + v.playerCount) % v.playerCount;
    const int nx2Size =
        (nx2 < static_cast<int>(v.players.size())) ? v.players[(size_t)nx2].handSize : 5;
    const bool hard = (diff_ == Difficulty::Hard);

    double bestScore = -1e18;
    int best = -1;

    for (int i = 0; i < n; ++i) {
        const Card& c = v.hand[(size_t)i];
        if (!c.playableOn(v.activeColor)) continue;
        if (c.kind == Kind::WildDrawFour && v.holdsActiveColor) continue;

        // What this move leaves me holding.
        int myColor = 0;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            if (v.hand[(size_t)j].color == c.color) myColor++;
        }

        double s = 0.0;
        if (n == 1) s += 10000.0;  // go out now

        switch (c.kind) {
            case Kind::Skip:
                s += 40.0 + static_cast<double>(nxSize) * 2.0;
                break;
            case Kind::Reverse:
                if (v.playerCount == 2) {
                    s += 40.0 + static_cast<double>(nxSize) * 2.0;
                } else {
                    // Direction flips: the seat after next is the one skipped.
                    s += 15.0 + static_cast<double>(nx2Size) * 2.0;
                }
                break;
            case Kind::DrawTwo:
                s += 55.0 + static_cast<double>(nxSize) * 4.0;
                break;
            case Kind::Wild:
                s -= 22.0;  // costly, but keeps full control of the colour
                break;
            case Kind::WildDrawFour:
                s += 60.0 + static_cast<double>(nxSize) * 5.0;
                break;
            case Kind::Number:
                s += 10.0;
                break;
        }

        // Every extra card of this colour left in hand is a gift to opponents.
        s -= static_cast<double>(myColor) * 9.0;
        // Keeping a second card of the colour keeps options open.
        if (myColor == 0 && n > 2) s += 6.0;
        // Dump expensive cards: opponents score from what we are left holding.
        s += static_cast<double>(c.points()) * 0.25;
        // Do not hand a near-out opponent a colour they can answer.
        if (nxSize <= 2) {
            s += c.isWild() ? 25.0 : -18.0;
        }

        if (hard) {
            // How loaded the untracked pool is with this colour. Clamped so a
            // big opponent hand cannot make every normal card look terrible
            // (which would leave the AI spamming wilds).
            const double colDensity = c.isWild() ? 0.25 : density(c.color);
            const int weight = std::min(nxSize, 12);
            s -= colDensity * 14.0 * static_cast<double>(weight);
            if (myColor > 0 && nxSize <= 3) s -= 6.0;
            if (v.drawCount < 10 && c.isWild() && n > 3) s -= 15.0;
            // Prefer keeping disruption cards for when opponents are loaded up.
            if (c.isSpecial() && !c.isWild() && nxSize >= 5) s += 8.0;
        } else {
            s += rnd(0, 6);
        }

        if (s > bestScore) {
            bestScore = s;
            best = i;
        }
    }
    return best;
}

}  // namespace uno