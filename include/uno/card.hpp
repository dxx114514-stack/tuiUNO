// Card model for a standard 108-card UNO deck.
#pragma once

#include <cstdint>
#include <string>

namespace uno {

enum class Color : uint8_t { None = 0, Red = 1, Yellow = 2, Green = 3, Blue = 4 };
enum class Kind : uint8_t {
    Number = 0,
    Skip = 1,
    Reverse = 2,
    DrawTwo = 3,
    Wild = 4,
    WildDrawFour = 5,
};

inline constexpr int kNumColors = 4;

inline Color nextColor(Color c) {
    return static_cast<Color>((static_cast<int>(c) % kNumColors) + 1);
}

const char* colorEn(Color c);
const char* colorZh(Color c);
char colorLetter(Color c);

struct Card {
    Color color = Color::None;
    Kind kind = Kind::Number;
    int value = 0;  // 0..9 for Number, -1 otherwise

    static Card number(Color c, int v) { return Card{c, Kind::Number, v}; }
    static Card skip(Color c) { return Card{c, Kind::Skip, -1}; }
    static Card reverse(Color c) { return Card{c, Kind::Reverse, -1}; }
    static Card drawTwo(Color c) { return Card{c, Kind::DrawTwo, -1}; }
    static Card wild() { return Card{Color::None, Kind::Wild, -1}; }
    static Card wildFour() { return Card{Color::None, Kind::WildDrawFour, -1}; }

    bool isWild() const { return kind == Kind::Wild || kind == Kind::WildDrawFour; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isSpecial() const { return kind != Kind::Number; }

    // A card is playable when it matches the color currently in effect.
    // Wilds are always playable; the +4 legality restriction is checked separately.
    bool playableOn(Color active) const {
        if (isWild()) return true;
        return active != Color::None && color == active;
    }

    int points() const {
        if (kind == Kind::Number) return value;
        if (kind == Kind::Wild || kind == Kind::WildDrawFour) return 50;
        return 20;
    }

    // Short ASCII tag used in logs and tests, e.g. "R7", "Y-S", "G+2", "*", "+4".
    std::string tag() const;
    // Unique identifier for a card kind, used for deck counting.
    uint32_t uid() const;
    // Unicode glyph drawn on the card face.
    char glyph() const;
};

std::string cardNameEn(const Card& c);
std::string cardNameZh(const Card& c);

}  // namespace uno