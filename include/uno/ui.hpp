// Terminal rendering: board, hands, menus, prompts.
#pragma once

#include <string>
#include <vector>

#include "uno/console.hpp"
#include "uno/game.hpp"

namespace uno {

struct MenuItem {
    std::string label;
    std::string hint;
    bool enabled = true;
};

// Full-screen menu with keyboard navigation.
class Menu {
  public:
    explicit Menu(std::string title) : title_(std::move(title)) {}
    void add(const std::string& label, const std::string& hint = "", bool enabled = true);
    // Returns the chosen index, or -1 when the user quit/cancelled.
    int run(const std::string& footer = "");
    int size() const { return static_cast<int>(items_.size()); }
    void clear() { items_.clear(); }

  private:
    void draw(int sel) const;
    std::string title_;
    std::string footer_;
    std::vector<MenuItem> items_;
};

// Renders the current game state and collects the local player's action.
class GameUI {
  public:
    // Draws the board. `v` is the local player's view.
    void drawBoard(const View& v);
    // Board plus a "waiting for <name>" line; returns immediately.
    void drawWaiting(const View& v);
    // Reads one action for the local player, blocking until it is submitted.
    Action promptAction(const View& v);

    void logEvent(const Event& e, const View& v);
    void pushChat(const std::string& from, const std::string& text);
    void setLocalSeat(int seat) { localSeat_ = seat; }

  private:
    void handleColorKey(Action& pending, const KeyEvent& k, bool& done);
    void handlePenaltyKey(const View& v, Action& pending, const KeyEvent& k, bool& done);
    void handlePlayKey(const View& v, Action& pending, const KeyEvent& k, bool& done);
    void drawHand(const View& v, int highlight);
    void drawPrompt(const View& v, const std::string& extra);
    void logLine(const std::string& s);

    int localSeat_ = 0;
    int highlight_ = 0;
    std::vector<std::string> chat_;
};

// Human-readable rendering of a single event.
std::string describeEvent(const Event& e, const View& v);
// Prompt lines for the current phase, already localised and filled in.
std::string promptForPlay(const View& v);
std::string promptForPenalty(const View& v);
std::string promptForColor(const View& v);

// Text helpers (ANSI-aware: escape sequences do not count toward the width).
std::string padTo(const std::string& s, int width);
std::string centerTo(const std::string& s, int width);
int visibleWidth(const std::string& s);

// Final standings / result screen. Returns true when the user wants another
// round.
bool showMatchResult(const View& v);

}  // namespace uno