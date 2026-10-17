#include "uno/session.hpp"

namespace uno {

MatchSession::MatchSession(Rules rules, uint32_t seed)
    : rules_(rules), seed_(seed), game_(rules, seed) {}

void MatchSession::addController(std::unique_ptr<Controller> c) {
    if (!c) return;
    const int seat = static_cast<int>(controllers_.size());
    game_.setName(seat, c->name());
    controllers_.push_back(std::move(c));
    connected_.push_back(true);
    isBot_.push_back(false);
}

void MatchSession::setSeatName(int seat, const std::string& name) {
    game_.setName(seat, name);
}

void MatchSession::setSeatConnected(int seat, bool connected) {
    if (seat >= 0 && seat < static_cast<int>(connected_.size())) connected_[(size_t)seat] = connected;
}

void MatchSession::setSeatBot(int seat, bool isBot) {
    if (seat >= 0 && seat < static_cast<int>(isBot_.size())) isBot_[(size_t)seat] = isBot;
}

bool MatchSession::seatIsRemote(int seat) const {
    if (seat < 0 || seat >= static_cast<int>(controllers_.size())) return false;
    return controllers_[(size_t)seat]->isRemote();
}

void MatchSession::startGame() { game_.start(); }

void MatchSession::broadcast(const UpdateSink& sink) {
    for (int seat = 0; seat < static_cast<int>(controllers_.size()); ++seat) {
        View v = game_.viewFor(seat);
        if (seat < static_cast<int>(connected_.size())) v.players[(size_t)seat].connected = connected_[(size_t)seat];
        if (seat < static_cast<int>(isBot_.size())) v.players[(size_t)seat].isBot = isBot_[(size_t)seat];
        controllers_[(size_t)seat]->observe(v);
        if (sink) sink(seat, v);
    }
}

SessionResult MatchSession::run(const UpdateSink& sink, const KeepGoing& keepGoing) {
    broadcast(sink);

    while (!game_.matchOver()) {
        if (keepGoing && !keepGoing()) return SessionResult::Aborted;

        const int seat = game_.current();
        if (seat < 0 || seat >= static_cast<int>(controllers_.size())) return SessionResult::Aborted;

        View mine = game_.viewFor(seat);
        if (seat < static_cast<int>(connected_.size())) mine.players[(size_t)seat].connected = connected_[(size_t)seat];
        Action action = controllers_[(size_t)seat]->awaitAction(mine);

        if (action.type == ActType::None) return SessionResult::Aborted;

        game_.submit(seat, action);
        broadcast(sink);
    }
    return SessionResult::Completed;
}

}  // namespace uno