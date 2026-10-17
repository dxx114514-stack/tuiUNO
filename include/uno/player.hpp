// Seat controllers. A seat can be a human at this terminal, a bot, or a remote
// player on another machine; the match loop does not care which.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "uno/ai.hpp"
#include "uno/game.hpp"
#include "uno/ui.hpp"

namespace uno {

class Controller {
  public:
    virtual ~Controller() = default;
    virtual std::string name() const = 0;
    virtual bool isBot() const { return false; }
    virtual bool isRemote() const { return false; }
    // Called after every state transition so stateful controllers can update.
    virtual void observe(const View& v) { (void)v; }
    // Blocks until this seat has an action to submit.
    virtual Action awaitAction(const View& v) = 0;
};

class HumanController : public Controller {
  public:
    HumanController(std::string name, GameUI& ui);
    std::string name() const override { return name_; }
    Action awaitAction(const View& v) override;

  private:
    std::string name_;
    GameUI& ui_;
};

class BotController : public Controller {
  public:
    BotController(std::string name, Difficulty d, uint32_t seed, int seat);
    std::string name() const override { return name_; }
    bool isBot() const override { return true; }
    void observe(const View& v) override;
    Action awaitAction(const View& v) override;

  private:
    std::string name_;
    Bot bot_;
};

class RemoteController : public Controller {
  public:
    // Resolves the next action for a session; returns false on disconnect or
    // when the player abandons the game.
    using Source = std::function<bool(int sessionId, Action&)>;

    RemoteController(std::string name, int sessionId);
    std::string name() const override { return name_; }
    bool isRemote() const override { return true; }
    void setSource(Source src) { source_ = std::move(src); }
    Action awaitAction(const View& v) override;

  private:
    // A legal, conservative move used when the remote player is gone or slow.
    static Action fallback(const View& v);
    std::string name_;
    int sessionId_ = -1;
    Source source_;
    bool abandoned_ = false;
};

}  // namespace uno