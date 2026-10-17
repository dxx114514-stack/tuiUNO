#include "uno/card.hpp"

namespace uno {

const char* colorEn(Color c) {
    switch (c) {
        case Color::Red: return "Red";
        case Color::Yellow: return "Yellow";
        case Color::Green: return "Green";
        case Color::Blue: return "Blue";
        default: return "None";
    }
}

const char* colorZh(Color c) {
    switch (c) {
        case Color::Red: return "红";
        case Color::Yellow: return "黄";
        case Color::Green: return "绿";
        case Color::Blue: return "蓝";
        default: return "无";
    }
}

char colorLetter(Color c) {
    switch (c) {
        case Color::Red: return 'R';
        case Color::Yellow: return 'Y';
        case Color::Green: return 'G';
        case Color::Blue: return 'B';
        default: return '-';
    }
}

std::string Card::tag() const {
    if (kind == Kind::Wild) return "*";
    if (kind == Kind::WildDrawFour) return "+4";
    char buf[8];
    switch (kind) {
        case Kind::Skip: buf[1] = 'S'; break;
        case Kind::Reverse: buf[1] = 'v'; break;
        case Kind::DrawTwo: buf[1] = '+'; break;
        default:
            buf[1] = static_cast<char>('0' + value);
            break;
    }
    buf[0] = colorLetter(color);
    buf[2] = '\0';
    return std::string(buf);
}

uint32_t Card::uid() const {
    if (kind == Kind::Wild) return 200;
    if (kind == Kind::WildDrawFour) return 201;
    const uint32_t base = static_cast<uint32_t>(color) * 20u;
    switch (kind) {
        case Kind::Number: return base + static_cast<uint32_t>(value);
        case Kind::Skip: return base + 12;
        case Kind::Reverse: return base + 13;
        case Kind::DrawTwo: return base + 14;
        default: break;
    }
    return base + 15;
}

char Card::glyph() const {
    if (kind == Kind::Wild) return '*';
    if (kind == Kind::WildDrawFour) return '+';
    switch (kind) {
        case Kind::Skip: return 'S';
        case Kind::Reverse: return 'v';
        default: return static_cast<char>('0' + value);
    }
}

std::string cardNameEn(const Card& c) {
    std::string base;
    switch (c.kind) {
        case Kind::Number: base = std::to_string(c.value); break;
        case Kind::Skip: base = "Skip"; break;
        case Kind::Reverse: base = "Reverse"; break;
        case Kind::DrawTwo: base = "Draw Two"; break;
        case Kind::Wild: base = "Wild"; break;
        case Kind::WildDrawFour: base = "Wild Draw Four"; break;
    }
    if (c.isWild()) return base;
    return std::string(colorEn(c.color)) + " " + base;
}

std::string cardNameZh(const Card& c) {
    std::string base;
    switch (c.kind) {
        case Kind::Number: base = std::to_string(c.value); break;
        case Kind::Skip: base = "禁手"; break;
        case Kind::Reverse: base = "反转"; break;
        case Kind::DrawTwo: base = "加二"; break;
        case Kind::Wild: base = "万能"; break;
        case Kind::WildDrawFour: base = "万能加四"; break;
    }
    if (c.isWild()) return base;
    return std::string(colorZh(c.color)) + base;
}

}  // namespace uno
