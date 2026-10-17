// Draw pile + discard pile with optional reshuffle.
#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "uno/card.hpp"

namespace uno {

// The canonical 108-card deck, in a fixed order.
std::vector<Card> standardDeck();

class Deck {
  public:
    explicit Deck(uint32_t seed = 0);

    // Builds and shuffles the standard 108-card deck.
    void reset();

    // Draws from the draw pile, recycling the discard pile when needed.
    // Returns false when the deck is genuinely exhausted (everything is in
    // players' hands plus the single face-up card); no card is ever invented.
    bool tryDrawOne(Card& out);
    // Draws as many cards as are available, up to `n`.
    std::vector<Card> draw(int n);

    void discard(const Card& c);

    bool empty() const { return drawPile_.empty(); }
    int drawCount() const { return static_cast<int>(drawPile_.size()); }
    int discardCount() const { return static_cast<int>(discardPile_.size()); }
    int totalRemaining() const { return drawCount() + discardCount(); }

    const Card& top() const { return discardPile_.back(); }
    bool hasTop() const { return !discardPile_.empty(); }

    // Moves every discarded card except the top one back into the draw pile.
    // Returns false if there is nothing to recycle.
    bool reshuffleDiscard();

    // All cards currently outside of players' hands (draw pile + discard).
    std::vector<Card> allSeen() const {
        std::vector<Card> v;
        v.reserve(drawPile_.size() + discardPile_.size());
        v.insert(v.end(), drawPile_.begin(), drawPile_.end());
        v.insert(v.end(), discardPile_.begin(), discardPile_.end());
        return v;
    }

    uint32_t seed() const { return seed_; }

  private:
    std::vector<Card> drawPile_;
    std::vector<Card> discardPile_;
    std::mt19937 rng_;
    uint32_t seed_ = 0;
};

}  // namespace uno