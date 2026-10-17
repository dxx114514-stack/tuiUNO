// Tunable rule set, shared by the engine, the UI and the network protocol.
#pragma once

#include <cstdint>
#include <string>

namespace uno {

struct Rules {
    int playerCount = 4;      // 2..4
    int initialCards = 7;     // 3..10
    int targetScore = 500;    // match winner is the first to reach this

    // Official UNO applies a Draw Two immediately; stacking it onto the next
    // Draw Two is a common house rule and costs the penalty pile momentum.
    bool stackDrawTwo = false;
    bool wildFourChallenge = true; // may challenge an illegal Wild Draw Four
    bool wildFourStack = false;    // may stack a Wild Draw Four on a Wild Draw Four
    bool mustPlay = false;         // if true, drawing while a legal play exists is forbidden
    bool recycleDiscard = true;    // reshuffle the discard pile when the draw pile runs out
    bool unoPenalty = true;        // missing the UNO call costs 2 cards
    bool swapHands = false;        // "7-7" opening swap
    bool teams = false;            // 4-player team mode (seat 0+2 vs 1+3)

    bool teamPlay() const { return teams && playerCount == 4; }
    bool operator==(const Rules& o) const;
    bool operator!=(const Rules& o) const { return !(*this == o); }
    std::string describe() const;
};

}  // namespace uno