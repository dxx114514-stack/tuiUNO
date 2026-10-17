#include "uno/player.hpp"

namespace uno {

HumanController::HumanController(std::string name, GameUI& ui)
    : name_(std::move(name)), ui_(ui) {}

Action HumanController::awaitAction(const View& v) {
    ui_.drawBoard(v);
    return ui_.promptAction(v);
}

BotController::BotController(std::string name, Difficulty d, uint32_t seed, int seat)
    : name_(std::move(name)), bot_(d, seed) {
    bot_.setSeat(seat);
}

void BotController::observe(const View& v) { bot_.observe(v); }

Action BotController::awaitAction(const View& v) { return bot_.decide(v); }

RemoteController::RemoteController(std::string name, int sessionId)
    : name_(std::move(name)), sessionId_(sessionId) {}

Action RemoteController::fallback(const View& v) {
    Action a;
    switch (v.phase) {
        case Phase::PickColor: {
            // Keep whatever colour we hold most of.
            const Color order[kNumColors] = {Color::Red, Color::Yellow, Color::Green, Color::Blue};
            int best = 0;
            Color bestColor = v.activeColor != Color::None ? v.activeColor : Color::Red;
            for (Color c : order) {
                int n = 0;
                for (const Card& x : v.hand) {
                    if (x.color == c) ++n;
                }
                if (n > best) {
                    best = n;
                    bestColor = c;
                }
            }
            a.type = ActType::ChooseColor;
            a.color = bestColor;
            return a;
        }
        case Phase::Penalty: {
            if (v.canStack) {
                for (size_t i = 0; i < v.hand.size(); ++i) {
                    const Card& c = v.hand[i];
                    if (c.kind == v.pendingCard.kind && c.playableOn(v.activeColor)) {
                        a.type = ActType::Play;
                        a.index = static_cast<int>(i);
                        return a;
                    }
                }
            }
            if (v.challengeOpen) {
                // Accepting is the safe default: the odds are usually against us.
                a.type = ActType::Accept;
                return a;
            }
            a.type = ActType::Accept;
            return a;
        }
        case Phase::Play: {
            if (v.callUnoAvailable) {
                a.type = ActType::CallUno;
                return a;
            }
            if (v.catchTarget >= 0) {
                a.type = ActType::Catch;
                a.target = v.catchTarget;
                return a;
            }
            if (v.canPlay) {
                for (size_t i = 0; i < v.hand.size(); ++i) {
                    const Card& c = v.hand[i];
                    if (!c.playableOn(v.activeColor)) continue;
                    if (c.kind == Kind::WildDrawFour && v.holdsActiveColor) continue;
                    a.type = ActType::Play;
                    a.index = static_cast<int>(i);
                    return a;
                }
            }
            if (v.canDraw) {
                a.type = ActType::Draw;
                return a;
            }
            if (v.canPass) {
                a.type = ActType::Pass;
                return a;
            }
            a.type = ActType::None;
            return a;
        }
        default:
            a.type = ActType::None;
            return a;
    }
}

Action RemoteController::awaitAction(const View& v) {
    if (abandoned_ || !source_) return fallback(v);
    Action a;
    if (!source_(sessionId_, a) || a.type == ActType::None) {
        abandoned_ = true;
        return fallback(v);
    }
    return a;
}

}  // namespace uno