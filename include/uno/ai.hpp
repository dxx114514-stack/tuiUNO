// CPU opponents. Easy plays roughly at random, Normal applies solid heuristics,
// Hard counts the deck and plays to set opponents up.
#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "uno/game.hpp"

namespace uno {

enum class Difficulty : uint8_t { Easy = 0, Normal = 1, Hard = 2 };

Difficulty difficultyFromInt(int v);
const char* difficultyName(Difficulty d);  // English
std::string difficultyLabel(Difficulty d); // localised

class Bot {
  public:
    Bot(Difficulty diff, uint32_t seed);

    // Called after every state transition for every bot, so it can keep its
    // memory of what has been played.
    void observe(const View& v);
    // Called only when it is this bot's turn.
    Action decide(const View& v);

    Difficulty difficulty() const { return diff_; }
    void setSeat(int s) { seat_ = s; }
    int seat() const { return seat_; }

  private:
    // Card accounting -----------------------------------------------------
    void rebuildPool(const View& v);
    int poolCount() const;
    int poolCount(Color c) const;
    int poolCount(Kind k) const;
    double density(Color c) const;
    // Fraction of the untracked pool made up of cards of kind `k`; a proxy for
    // the chance that any given opponent still holds one.
    double chanceOpponentHas(Kind k, int opponent) const;
    // Expected number of colour `c` cards sitting in a hand of `handSize`
    // cards, derived from how many untracked cards of that colour are left.
    double expectedIn(int handSize, Color c) const;
    int handColorCount(Color c) const;

    // Play selection ------------------------------------------------------
    Action decidePlay(const View& v);
    Action decidePenalty(const View& v);
    Action decidePickColor(const View& v);
    Color chooseColor(const View& v, int seat);
    int bestPlayIndex(const View& v);

    int rnd(int loInclusive, int hiInclusive);
    double rnd01();
    bool chance(double p);

    Difficulty diff_;
    std::mt19937 rng_;
    int seat_ = 0;

    std::map<uint32_t, int> total_;   // cards of each kind in a full deck
    std::map<uint32_t, int> seen_;    // publicly seen since the last recycle
    std::map<uint32_t, int> hand_;    // this bot's hand as of the last observe
    bool started_ = false;
};

}  // namespace uno