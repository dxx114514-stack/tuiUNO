#include "uno/rules.hpp"

namespace uno {

bool Rules::operator==(const Rules& o) const {
    return playerCount == o.playerCount && initialCards == o.initialCards &&
           targetScore == o.targetScore && stackDrawTwo == o.stackDrawTwo &&
           wildFourChallenge == o.wildFourChallenge && wildFourStack == o.wildFourStack &&
           mustPlay == o.mustPlay && recycleDiscard == o.recycleDiscard &&
           unoPenalty == o.unoPenalty && swapHands == o.swapHands && teams == o.teams;
}

std::string Rules::describe() const {
    std::string s;
    s += std::to_string(playerCount) + "P";
    s += " x" + std::to_string(initialCards);
    s += " target=" + std::to_string(targetScore);
    if (stackDrawTwo) s += " +stack2";
    if (wildFourChallenge) s += " +chal4";
    if (wildFourStack) s += " +stack4";
    if (mustPlay) s += " mustplay";
    if (unoPenalty) s += " unopenalty";
    if (swapHands) s += " swap77";
    if (teamPlay()) s += " teams";
    return s;
}

}  // namespace uno