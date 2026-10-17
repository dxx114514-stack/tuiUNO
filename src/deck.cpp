#include "uno/deck.hpp"

#include <algorithm>

namespace uno {

namespace {

std::vector<Card> makeStandardDeck() {
    std::vector<Card> deck;
    deck.reserve(108);
    const Color colors[kNumColors] = {Color::Red, Color::Yellow, Color::Green, Color::Blue};
    for (Color c : colors) {
        deck.push_back(Card::number(c, 0));
        for (int v = 1; v <= 9; ++v) {
            deck.push_back(Card::number(c, v));
            deck.push_back(Card::number(c, v));
        }
        deck.push_back(Card::skip(c));
        deck.push_back(Card::skip(c));
        deck.push_back(Card::reverse(c));
        deck.push_back(Card::reverse(c));
        deck.push_back(Card::drawTwo(c));
        deck.push_back(Card::drawTwo(c));
    }
    for (int i = 0; i < 4; ++i) deck.push_back(Card::wild());
    for (int i = 0; i < 4; ++i) deck.push_back(Card::wildFour());
    return deck;
}
}  // namespace

std::vector<Card> standardDeck() { return makeStandardDeck(); }

Deck::Deck(uint32_t seed) : rng_(seed), seed_(seed) { reset(); }

void Deck::reset() {
    drawPile_ = standardDeck();
    std::shuffle(drawPile_.begin(), drawPile_.end(), rng_);
    discardPile_.clear();
}

bool Deck::tryDrawOne(Card& out) {
    if (drawPile_.empty()) reshuffleDiscard();
    if (drawPile_.empty()) return false;
    out = drawPile_.back();
    drawPile_.pop_back();
    return true;
}

std::vector<Card> Deck::draw(int n) {
    std::vector<Card> out;
    out.reserve(static_cast<size_t>(n < 0 ? 0 : n));
    for (int i = 0; i < n; ++i) {
        Card c;
        if (!tryDrawOne(c)) break;
        out.push_back(c);
    }
    return out;
}

void Deck::discard(const Card& c) { discardPile_.push_back(c); }

bool Deck::reshuffleDiscard() {
    if (discardPile_.size() < 2) return false;
    // Keep the top card face up on the table.
    Card keep = discardPile_.back();
    for (size_t i = 0; i + 1 < discardPile_.size(); ++i) {
        drawPile_.push_back(discardPile_[i]);
    }
    discardPile_.clear();
    discardPile_.push_back(keep);
    std::shuffle(drawPile_.begin(), drawPile_.end(), rng_);
    return true;
}

}  // namespace uno