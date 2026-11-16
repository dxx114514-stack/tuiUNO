#include "uno/ui.hpp"

#include <algorithm>

#include "uno/ai.hpp"
#include "uno/console.hpp"
#include "uno/i18n.hpp"

namespace uno {

namespace {

const char* kReset = "\x1b[0m";
const char* kBold = "\x1b[1m";
const char* kDim = "\x1b[2m";
const char* kGreen = "\x1b[32m";
const char* kRed = "\x1b[31m";
const char* kYellow = "\x1b[33m";
const char* kBlue = "\x1b[34m";
const char* kCyan = "\x1b[36m";
const char* kWhite = "\x1b[37m";
const char* kBgGray = "\x1b[90m";

const char* colorAnsi(Color c) {
    switch (c) {
        case Color::Red: return kRed;
        case Color::Yellow: return kYellow;
        case Color::Green: return kGreen;
        case Color::Blue: return kBlue;
        default: return kWhite;
    }
}

const char* kindColor(Color c) { return colorAnsi(c); }

// Terminal column width of one Unicode code point. This is a compact wcwidth:
// combining marks and controls are zero columns, CJK/fullwidth/emoji are two.
int codepointWidth(uint32_t cp) {
    if (cp == 0) return 0;
    if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return 0;
    if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
        (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
        (cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xfe20 && cp <= 0xfe2f) ||
        cp == 0x200b || cp == 0x200c || cp == 0x200d || cp == 0xfeff) {
        return 0;
    }
    if ((cp >= 0x1100 && cp <= 0x115f) || cp == 0x2329 || cp == 0x232a ||
        (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
        (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
        (cp >= 0xfe10 && cp <= 0xfe19) || (cp >= 0xfe30 && cp <= 0xfe6f) ||
        (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) ||
        (cp >= 0x1f300 && cp <= 0x1f64f) || (cp >= 0x1f900 && cp <= 0x1f9ff) ||
        (cp >= 0x20000 && cp <= 0x3fffd)) {
        return 2;
    }
    return 1;
}

int displayWidth(const std::string& s) {
    int n = 0;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1b) {
            // Recognised ANSI escape: CSI (ESC [ ... final byte). Anything else
            // is a lone control and contributes no columns.
            if (i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size()) {
                    const unsigned char f = static_cast<unsigned char>(s[i++]);
                    if (f >= 0x40 && f <= 0x7e) break;
                }
            } else {
                ++i;
            }
            continue;
        }

        uint32_t cp = c;
        size_t width = 1;
        if ((c & 0x80) == 0) {
            cp = c;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) {
            cp = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3fu);
            width = 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < s.size()) {
            cp = ((c & 0x0fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 1]) & 0x3fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3fu);
            width = 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) |
                 ((static_cast<unsigned char>(s[i + 1]) & 0x3fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3fu) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3fu);
            width = 4;
        } else if ((c & 0xc0) == 0x80) {
            // Stray continuation byte: invalid input, no visible cell.
            cp = 0;
        }
        n += codepointWidth(cp);
        i += width;
    }
    return n;
}

std::string pad(const std::string& s, int width) {
    const int w = displayWidth(s);
    if (w >= width) return s;
    return s + std::string(static_cast<size_t>(width - w), ' ');
}

std::string centered(const std::string& s, int width) {
    const int w = displayWidth(s);
    if (w >= width) return s;
    const int left = (width - w) / 2;
    return std::string(static_cast<size_t>(left), ' ') + s;
}

std::string repeatStr(const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

// A single card face, e.g. "[R7]" with the glyph tinted by the card colour.
std::string cardFace(const Card& c, bool highlighted) {
    const char* cc = kindColor(c.color);
    std::string face = std::string(highlighted ? "\x1b[1;37m" : kWhite);
    face += "[";
    face += cc;
    face += c.glyph();
    face += kReset;
    face += "]";
    return face;
}

std::string nameOf(const View& v, int seat) {
    if (seat < 0 || seat >= static_cast<int>(v.players.size())) return "?";
    const std::string& n = v.players[(size_t)seat].name;
    return n.empty() ? std::string("P") + S(seat + 1) : n;
}

std::string colorWord(Color c) {
    return I18n::instance().isZh() ? colorZh(c) : colorEn(c);
}

}  // namespace

// ---------------------------------------------------------------------------
// text helpers
// ---------------------------------------------------------------------------

int visibleWidth(const std::string& s) { return displayWidth(s); }

std::string padTo(const std::string& s, int width) {
    const int w = displayWidth(s);
    if (w >= width) return s;
    return s + std::string(static_cast<size_t>(width - w), ' ');
}

std::string centerTo(const std::string& s, int width) {
    const int w = displayWidth(s);
    if (w >= width) return s;
    const int left = (width - w) / 2;
    return std::string(static_cast<size_t>(left), ' ') + s;
}

// ---------------------------------------------------------------------------
// event text
// ---------------------------------------------------------------------------

std::string describeEvent(const Event& e, const View& v) {
    auto who = [&](int seat) { return nameOf(v, seat); };
    switch (e.type) {
        case EvType::Started:
            return Fmt("ui.round_start", v.roundNo);
        case EvType::Turn:
            return Fmt("ev.turn", who(e.player));
        case EvType::Drawn:
            return Fmt("ev.drawn", who(e.player), e.value);
        case EvType::Recycle:
            return tr("ev.recycle");
        case EvType::Played:
            return Fmt("ev.played", who(e.player),
                       I18n::instance().isZh() ? cardNameZh(e.card) : cardNameEn(e.card));
        case EvType::ColorChosen:
            return Fmt("ev.color", colorWord(e.color));
        case EvType::Skipped:
            return Fmt("ev.skipped", who(e.player));
        case EvType::Reversed:
            return tr("ev.reversed");
        case EvType::Penalty:
            return Fmt("ev.penalty", who(e.player), e.value);
        case EvType::Stacked:
            return Fmt("ev.stacked", who(e.player), e.value);
        case EvType::ChallengeOK:
            return Fmt("ev.chal_ok", who(e.player), who(e.value));
        case EvType::ChallengeBad:
            return Fmt("ev.chal_bad", who(e.player));
        case EvType::UnoSaid:
            return Fmt("ev.uno", who(e.player));
        case EvType::UnoCaught:
            return Fmt("ev.caught", who(e.player), who(e.value));
        case EvType::HandSwapped:
            return tr("ev.swap");
        case EvType::PlayerOut:
            return Fmt("ev.out", who(e.player));
        case EvType::RoundOver:
            return Fmt("end.points_awarded", who(e.player), e.value);
        case EvType::MatchOver:
            return Fmt("ev.winner", who(e.player));
        case EvType::Rejected:
            return "";
        default:
            return "";
    }
}

std::string promptForColor(const View& v) {
    (void)v;
    return tr("ui.choose_color");
}

std::string promptForPenalty(const View& v) {
    if (v.challengeOpen) {
        const std::string who = v.current >= 0 ? nameOf(v, (v.current + v.playerCount - 1) % v.playerCount)
                                               : std::string();
        return Fmt("ui.challenge_prompt", who,
                   I18n::instance().isZh() ? cardNameZh(v.pendingCard)
                                           : cardNameEn(v.pendingCard));
    }
    return Fmt("ui.pending_penalty", v.pendingDraw);
}

std::string promptForPlay(const View& v) {
    if (!v.canPlay) return tr("ui.no_play");
    std::string s = Fmt("ui.choose_card", v.hand.size());
    if (v.catchTarget >= 0) s += std::string(kYellow) + tr("ui.catch_hint") + kReset;
    return s;
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

void Menu::add(const std::string& label, const std::string& hint, bool enabled) {
    items_.push_back(MenuItem{label, hint, enabled});
}

void Menu::draw(int sel) const {
    const int width = std::min(consoleWidth(), 96);
    const int height = consoleHeight();
    clearScreen();
    println("");
    println(centered(std::string(kBold) + kCyan + title_ + kReset, width));
    println(centered(std::string(kDim) + repeatStr("=", std::min(width - 8, 60)) + kReset, width));
    println("");

    const int rows = std::max(1, height - 12);
    const int first = std::max(0, std::min(sel - rows / 2, static_cast<int>(items_.size()) - rows));
    for (int i = first; i < static_cast<int>(items_.size()) && i < first + rows; ++i) {
        const MenuItem& m = items_[(size_t)i];
        const bool on = (i == sel);
        std::string marker = on ? std::string(kBold) + kCyan + " > " : "   ";
        std::string label = pad(m.label, 34);
        std::string line = marker + label;
        line += m.enabled ? "" : std::string(kDim);
        if (!m.enabled) line += kReset;
        line += std::string(kDim) + pad("", 0) + m.hint + kReset;
        println(line);
    }
    println("");
    if (!footer_.empty()) println(centered(std::string(kDim) + footer_ + kReset, width));
    println(centered(std::string(kDim) + tr("menu.navigate") + kReset, width));
}

int Menu::run(const std::string& footer) {
    footer_ = footer;
    int sel = 0;
    for (;;) {
        // Skip disabled entries.
        if (!items_.empty() && !items_[(size_t)sel].enabled) {
            const int n = static_cast<int>(items_.size());
            for (int step = 1; step <= n; ++step) {
                const int cand = (sel + step) % n;
                if (items_[(size_t)cand].enabled) {
                    sel = cand;
                    break;
                }
            }
        }
        draw(sel);
        KeyEvent k = waitKey();
        const int n = static_cast<int>(items_.size());
        if (n == 0) return -1;
        if (k.key == Key::Up || k.isCharIgnoreCase('k')) sel = (sel - 1 + n) % n;
        else if (k.key == Key::Down || k.isCharIgnoreCase('j')) sel = (sel + 1) % n;
        else if (k.key == Key::Home) sel = 0;
        else if (k.key == Key::End) sel = n - 1;
        else if (k.key == Key::Enter || k.key == Key::Space) {
            if (items_[(size_t)sel].enabled) return sel;
        } else if (k.key == Key::CtrlC || k.key == Key::Escape) {
            return -1;
        }
    }
}

// ---------------------------------------------------------------------------
// GameUI
// ---------------------------------------------------------------------------

void GameUI::logLine(const std::string& s) {
    chat_.push_back(s);
    while (chat_.size() > 8) chat_.erase(chat_.begin());
}

void GameUI::logEvent(const Event& e, const View& v) {
    const std::string s = describeEvent(e, v);
    if (!s.empty()) logLine(s);
}

void GameUI::pushChat(const std::string& from, const std::string& text) {
    logLine(std::string(kCyan) + "<" + from + "> " + text + kReset);
}

void GameUI::drawBoard(const View& v) {
    const int width = std::min(consoleWidth(), 100);
    const int height = consoleHeight();
    clearScreen();

    // --- header ---
    std::string title = std::string(kBold) + "UNO" + kReset + std::string(kDim) + "  round " +
                        kReset + S(v.roundNo) + std::string(kDim) + "  turn " + kReset +
                        S(v.turnNo);
    println(centered(title, width));
    println(centered(std::string(kDim) + std::string(v.direction > 0 ? "-> clockwise" : "<- counter-clockwise") + kReset, width));
    println("");

    // --- table centre ---
    const Card& top = v.top;
    std::string face = std::string("\x1b[1;37m") + "[ " + colorAnsi(top.color) + top.glyph() +
                       " ]" + kReset;
    println(centered(std::string(kDim) + "table" + kReset, width));
    println(centered(face, width));
    std::string colorLine = std::string(kDim) + "color in effect: " + kReset + colorAnsi(v.activeColor) +
                            colorWord(v.activeColor) + kReset;
    if (v.pendingDraw > 0) {
        colorLine += std::string(kYellow) + "   pending +" + S(v.pendingDraw) + kReset;
    }
    println(centered(colorLine, width));
    println(centered(std::string(kDim) + "draw pile " + kReset + S(v.drawCount) +
                     std::string(kDim) + "   discard " + kReset + S(v.discardCount) + kReset,
                     width));
    println("");

    // --- opponents ---
    for (int p = 0; p < v.playerCount; ++p) {
        if (p == localSeat_) continue;
        const PlayerInfo& pi = v.players[(size_t)p];
        const bool active = (v.phase != Phase::Over && v.current == p);
        std::string marker = active ? std::string(kBold) + kCyan + ">" + kReset : " ";
        std::string name = pad(pi.name, 14);
        std::string info = std::string(kDim) + pad(std::to_string(pi.handSize) + " cards", 10) +
                           "  " + kReset + pad("score " + S(pi.score), 12) + kReset;
        if (pi.unoCalled) info += std::string(kGreen) + "UNO!" + kReset;
        if (!pi.connected) info += std::string(kRed) + " [offline]" + kReset;
        println(" " + marker + name + info);
    }
    println("");

    // --- local status ---
    if (v.current == localSeat_ && v.phase != Phase::Over) {
        println(centered(std::string(kBold) + kYellow + tr("ui.your_turn") + kReset, width));
    }

    drawHand(v, highlight_);

    // --- log ---
    println("");
    const int logRows = std::max(2, height - 22);
    const size_t start = chat_.size() > (size_t)logRows ? chat_.size() - (size_t)logRows : 0;
    for (size_t i = start; i < chat_.size(); ++i) println(" " + chat_[i]);
}

void GameUI::drawWaiting(const View& v) {
    drawBoard(v);
    const int width = std::min(consoleWidth(), 100);
    println("");
    if (v.current >= 0 && v.current < v.playerCount) {
        const std::string who = nameOf(v, v.current);
        println(centerTo(std::string(kBold) + kYellow + Fmt("ui.wait", who) + kReset, width));
    }
    println(centerTo(std::string(kDim) + "/" + " chat   Esc = leave" + kReset, width));
}

void GameUI::drawHand(const View& v, int highlight) {
    const int width = std::min(consoleWidth(), 100);
    if (v.hand.empty()) {
        println(" " + std::string(kDim) + "(no cards)" + kReset);
        return;
    }

    // Faces, wrapped to the terminal width. Each face occupies 5 columns.
    std::string faces = " ";
    std::string ruler = " ";
    const size_t perRow = static_cast<size_t>((width - 4) / 5);
    for (size_t i = 0; i < v.hand.size(); ++i) {
        const bool on = (static_cast<int>(i) == highlight);
        if (i > 0 && i % perRow == 0) faces += "\r\n   ", ruler += "\r\n   ";
        if (on) faces += std::string(kBgGray) + std::string(kBold);
        faces += cardFace(v.hand[i], on);
        if (on) faces += kReset;
        faces += " ";
        const int digit = static_cast<int>((i + 1) % 10);
        ruler += on ? std::string(kBold) + kYellow : std::string(kDim);
        ruler += S(digit);
        ruler += " ";
    }
    println(faces);
    println(std::string(kDim) + "  " + ruler + kReset);
}

// ---------------------------------------------------------------------------

bool showMatchResult(const View& v) {
    const int width = std::min(consoleWidth(), 80);
    clearScreen();
    println("");
    println(centered(std::string(kBold) + kCyan + tr("end.title") + kReset, width));
    println("");

    const int champ = v.finalRank.empty() ? -1 : v.finalRank.front();
    std::string verdict;
    const char* vc = kReset;
    if (champ == v.myId) {
        verdict = tr("end.won");
        vc = kGreen;
    } else {
        verdict = tr("end.lost");
        vc = kRed;
    }
    println(centered(std::string(kBold) + vc + verdict + kReset, width));
    println("");
    println(centered(std::string(kDim) + tr("end.rank") + kReset, width));

    for (size_t i = 0; i < v.players.size(); ++i) {
        const PlayerInfo& pi = v.players[i];
        const int place = static_cast<int>(i) + 1;
        std::string line = "  " + S(place) + ". " + pad(pi.name, 16) +
                           pad("score " + S(pi.score), 16);
        if (static_cast<int>(i) == v.myId) line += std::string(kBold) + kYellow + "< you" + kReset;
        println(line);
    }
    println("");
    println(centered(std::string(kDim) + tr("common.press_enter") + kReset, width));
    KeyEvent k = waitKey();
    return !(k.key == Key::CtrlC || k.key == Key::Escape);
}

void GameUI::drawPrompt(const View& v, const std::string& extra) {
    const int width = std::min(consoleWidth(), 100);
    println("");
    if (!extra.empty()) println(centered(extra, width));

    switch (v.phase) {
        case Phase::PickColor:
            println(centered(std::string(kBold) + promptForColor(v) + kReset, width));
            println(centered(std::string(kDim) + "(R=red  Y=yellow  G=green  B=blue)" + kReset, width));
            break;
        case Phase::Penalty:
            println(centered(std::string(kBold) + kYellow + promptForPenalty(v) + kReset, width));
            break;
        case Phase::Play:
            println(centered(std::string(kBold) + kCyan + ">" + kReset, width));
            break;
        default:
            break;
    }
}

Action GameUI::promptAction(const View& v) {
    Action a;
    highlight_ = -1;
    if (!v.hand.empty()) highlight_ = 0;

    // Default suggestion: highlight the first legal card.
    if (highlight_ >= 0) {
        for (size_t i = 0; i < v.hand.size(); ++i) {
            const Card& c = v.hand[i];
            if (!c.playableOn(v.activeColor)) continue;
            if (c.kind == Kind::WildDrawFour && v.holdsActiveColor) continue;
            highlight_ = static_cast<int>(i);
            break;
        }
    }

    std::string hint;
    if (v.phase == Phase::Play) hint = promptForPlay(v);
    drawPrompt(v, hint);

    for (;;) {
        KeyEvent k = waitKey();

        // Escape and Ctrl+C abandon the turn, which the match loop reads as a
        // forfeit.
        if (k.key == Key::CtrlC || k.key == Key::Escape) {
            a.type = ActType::None;
            return a;
        }

        // Catch a missing UNO at any point during our turn.
        if (k.isCharIgnoreCase('c') && v.catchTarget >= 0) {
            a.type = ActType::Catch;
            a.target = v.catchTarget;
            return a;
        }
        // Say UNO.
        if (k.isCharIgnoreCase('u') && v.callUnoAvailable) {
            a.type = ActType::CallUno;
            return a;
        }

        switch (v.phase) {
            case Phase::PickColor: {
                bool done = false;
                handleColorKey(a, k, done);
                if (done) return a;
                break;
            }
            case Phase::Penalty: {
                bool done = false;
                handlePenaltyKey(v, a, k, done);
                if (done) return a;
                break;
            }
            case Phase::Play: {
                bool done = false;
                handlePlayKey(v, a, k, done);
                if (done) return a;
                break;
            }
            default:
                break;
        }
    }
}

void GameUI::handleColorKey(Action& pending, const KeyEvent& k, bool& done) {
    (void)pending;
    Action a;
    a.type = ActType::ChooseColor;
    if (k.isCharIgnoreCase('r') || k.isChar('1')) a.color = Color::Red;
    else if (k.isCharIgnoreCase('y') || k.isChar('2')) a.color = Color::Yellow;
    else if (k.isCharIgnoreCase('g') || k.isChar('3')) a.color = Color::Green;
    else if (k.isCharIgnoreCase('b') || k.isChar('4')) a.color = Color::Blue;
    else return;
    pending = a;
    done = true;
}

void GameUI::handlePenaltyKey(const View& v, Action& pending, const KeyEvent& k, bool& done) {
    (void)pending;
    Action a;
    if (v.challengeOpen && (k.isCharIgnoreCase('y') || k.isCharIgnoreCase('c'))) {
        a.type = ActType::Challenge;
        pending = a;
        done = true;
        return;
    }
    if (k.isCharIgnoreCase('n') || k.isCharIgnoreCase('a') || k.key == Key::Enter) {
        a.type = ActType::Accept;
        pending = a;
        done = true;
        return;
    }
    // Digits select a stackable card.
    if (v.canStack && k.key == Key::Char && k.ch >= '0' && k.ch <= '9') {
        const int idx = (k.ch == '0') ? 9 : (k.ch - '1');
        Action p;
        p.type = ActType::Play;
        p.index = idx;
        pending = p;
        done = true;
    }
}

void GameUI::handlePlayKey(const View& v, Action& pending, const KeyEvent& k, bool& done) {
    // Navigate the highlighted card.
    if (k.key == Key::Left || k.key == Key::Right || k.isCharIgnoreCase('h') ||
        k.isCharIgnoreCase('l')) {
        if (v.hand.empty()) return;
        const int n = static_cast<int>(v.hand.size());
        const int step = (k.key == Key::Left || k.isCharIgnoreCase('h')) ? -1 : 1;
        highlight_ = ((highlight_ < 0 ? 0 : highlight_) + step + n) % n;
        drawBoard(v);
        drawPrompt(v, tr("ui.hint_numbers"));
        return;
    }

    if (k.key == Key::Char && k.ch >= '0' && k.ch <= '9' && !v.hand.empty()) {
        const int idx = (k.ch == '0') ? 9 : (k.ch - '1');
        if (idx < static_cast<int>(v.hand.size())) {
            Action p;
            p.type = ActType::Play;
            p.index = idx;
            pending = p;
            done = true;
        }
        return;
    }
    if (k.key == Key::Enter && highlight_ >= 0) {
        Action p;
        p.type = ActType::Play;
        p.index = highlight_;
        pending = p;
        done = true;
        return;
    }
    if (k.isCharIgnoreCase('d') && v.canDraw) {
        Action p;
        p.type = ActType::Draw;
        pending = p;
        done = true;
        return;
    }
    if (k.isCharIgnoreCase('p') && v.canPass) {
        Action p;
        p.type = ActType::Pass;
        pending = p;
        done = true;
    }
}

}  // namespace uno