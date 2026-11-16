// Drives a match: owns the Game, hands each seat's turn to its controller and
// reports every state transition through a callback.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "uno/game.hpp"
#include "uno/player.hpp"

namespace uno {

enum class SessionResult {
    Completed,  // someone reached the target score
    Quit,       // a player left mid-match; the game was settled as a forfeit
    Aborted,    // the host pulled the plug / the keep-going poll said stop
};

class MatchSession {
  public:
    MatchSession(Rules rules, uint32_t seed);

    // Controllers must be added in seat order before start().
    void addController(std::unique_ptr<Controller> c);
    void setSeatName(int seat, const std::string& name);

    void startGame();

    // Patched into every view so clients see connection state and bot seats.
    void setSeatConnected(int seat, bool connected);
    void setSeatBot(int seat, bool isBot);

    // Called after every transition with the seat's own view.
    using UpdateSink = std::function<void(int seat, const View& v)>;
    // Polled each step; returning false aborts the match.
    using KeepGoing = std::function<bool()>;

    SessionResult run(const UpdateSink& sink, const KeepGoing& keepGoing);

    Game& game() { return game_; }
    int seatCount() const { return static_cast<int>(controllers_.size()); }
    bool seatIsRemote(int seat) const;

  private:
    void broadcast(const UpdateSink& sink);

    Rules rules_;
    uint32_t seed_;
    Game game_;
    std::vector<std::unique_ptr<Controller>> controllers_;
    std::vector<bool> connected_;
    std::vector<bool> isBot_;
};

}  // namespace uno