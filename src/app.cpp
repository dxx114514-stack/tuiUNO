#include "uno/app.hpp"

#include <algorithm>
#include <chrono>
#include <random>

#include "uno/ai.hpp"
#include "uno/console.hpp"
#include "uno/session.hpp"
#include "uno/ui.hpp"

namespace uno {

namespace {

std::string randomPlayerName() {
    static const char* first[] = {"Ada", "Neo",  "Trinity", "Morpheus", "Rex", "Loki",
                                  "Nova", "Pixel", "Zed",    "Byte",     "Echo", "Flux"};
    static const char* last[] = {"Uno", "Bot", "Ace",  "King", "Fox", "Wolf",
                                 "Star", "Ray", "Kid", "Pro"};
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> a(0, 11);
    std::uniform_int_distribution<int> b(0, 9);
    return std::string(first[a(rng)]) + last[b(rng)];
}

uint32_t resolveSeed(uint32_t requested) {
    if (requested != 0) return requested;
    return static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

int askInt(const std::string& prompt, int lo, int hi, int def) {
    for (;;) {
        const std::string s = readLine(prompt + " [" + S(def) + "]: ");
        if (s.empty()) return def;
        try {
            const int v = std::stoi(s);
            if (v >= lo && v <= hi) return v;
        } catch (...) {
        }
        println(tr("common.invalid"));
    }
}

bool askYesNo(const std::string& prompt, bool def) {
    for (;;) {
        const std::string s = readLine(prompt + " [Y/n]: ");
        if (s.empty()) return def;
        if (s[0] == 'y' || s[0] == 'Y') return true;
        if (s[0] == 'n' || s[0] == 'N') return false;
    }
}

std::string onOff(bool v) { return v ? tr("common.on") : tr("common.off"); }

// ---------------------------------------------------------------------------
// rule editor
// ---------------------------------------------------------------------------

enum RuleRow {
    kRowPlayers = 0,
    kRowCards,
    kRowTarget,
    kRowStack2,
    kRowChallenge,
    kRowStack4,
    kRowMustPlay,
    kRowRecycle,
    kRowUnoPenalty,
    kRowSwap77,
    kRowTeams,
    kRowDifficulty,
    kRowSeed,
    kRowReset,
    kRowBack,
};

void editRules(Rules& r, int& difficulty, uint32_t& seed) {
    for (;;) {
        Menu menu(tr("rules.title"));
        menu.add(tr("rules.players"), S(r.playerCount));
        menu.add(tr("rules.cards"), S(r.initialCards));
        menu.add(tr("rules.target"), S(r.targetScore));
        menu.add(tr("rules.stack2"), onOff(r.stackDrawTwo));
        menu.add(tr("rules.chal4"), onOff(r.wildFourChallenge));
        menu.add(tr("rules.stack4"), onOff(r.wildFourStack));
        menu.add(tr("rules.mustplay"), onOff(r.mustPlay));
        menu.add(tr("rules.recycle"), onOff(r.recycleDiscard));
        menu.add(tr("rules.unopenalty"), onOff(r.unoPenalty));
        menu.add(tr("rules.swap77"), onOff(r.swapHands));
        menu.add(tr("rules.teams"), onOff(r.teams), r.playerCount == 4);
        menu.add(tr("pve.diff2"), difficultyName(difficultyFromInt(difficulty)));
        menu.add(tr("rules.seed"), seed == 0 ? "random" : S(seed));
        menu.add(tr("rules.reset"), "");
        menu.add(tr("common.back"), "");

        const int pick = menu.run();
        if (pick < 0 || pick == kRowBack) return;
        switch (pick) {
            case kRowPlayers:
                r.playerCount = askInt(tr("rules.players"), 2, 4, r.playerCount);
                if (r.playerCount != 4) r.teams = false;
                break;
            case kRowCards:
                r.initialCards = askInt(tr("rules.cards"), 3, 10, r.initialCards);
                break;
            case kRowTarget:
                r.targetScore = askInt(tr("rules.target"), 50, 2000, r.targetScore);
                break;
            case kRowStack2: r.stackDrawTwo = !r.stackDrawTwo; break;
            case kRowChallenge: r.wildFourChallenge = !r.wildFourChallenge; break;
            case kRowStack4: r.wildFourStack = !r.wildFourStack; break;
            case kRowMustPlay: r.mustPlay = !r.mustPlay; break;
            case kRowRecycle: r.recycleDiscard = !r.recycleDiscard; break;
            case kRowUnoPenalty: r.unoPenalty = !r.unoPenalty; break;
            case kRowSwap77: r.swapHands = !r.swapHands; break;
            case kRowTeams:
                if (r.playerCount == 4) r.teams = !r.teams;
                break;
            case kRowDifficulty: {
                Menu dm(tr("rules.title"));
                for (int i = 0; i < 3; ++i) dm.add(difficultyLabel(difficultyFromInt(i)));
                const int d = dm.run();
                if (d >= 0) difficulty = d;
                break;
            }
            case kRowSeed: {
                const std::string s =
                    readLine(tr("rules.seed") + " (0 = random): ");
                try {
                    seed = static_cast<uint32_t>(std::stoul(s));
                } catch (...) {
                    seed = 0;
                }
                break;
            }
            case kRowReset:
                r = Rules{};
                difficulty = 1;
                seed = 0;
                println(tr("rules.reset"));
                break;
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// offline setup
// ---------------------------------------------------------------------------

struct OfflineSetup {
    int humanSeats = 1;
    int humanA = 0;
    int humanB = 1;
    int difficulty = 1;
    std::string name;
    bool cancelled = false;
};

OfflineSetup setupOffline(const AppConfig& cfg) {
    OfflineSetup s;
    s.difficulty = cfg.difficulty;
    s.name = randomPlayerName();

    {
        Menu menu(tr("menu.pve"));
        menu.add("1 human + " + S(cfg.rules.playerCount - 1) + " CPU", "");
        menu.add("2 humans + " + S(cfg.rules.playerCount - 2) + " CPU", "",
                 cfg.rules.playerCount >= 2);
        menu.add(tr("common.back"), "");
        const int pick = menu.run();
        if (pick < 0 || pick == 2) {
            s.cancelled = true;
            return s;
        }
        s.humanSeats = (pick == 0) ? 1 : 2;
    }

    if (s.humanSeats == 2) {
        Menu menu(tr("pve.select_seat"));
        menu.add("1 + 2", "");
        menu.add("1 + 3", "", cfg.rules.playerCount >= 3);
        menu.add("1 + 4", "", cfg.rules.playerCount >= 4);
        const int pick = menu.run();
        if (pick < 0) {
            s.cancelled = true;
            return s;
        }
        s.humanA = 0;
        s.humanB = (pick == 0) ? 1 : pick + 1;
    }

    const std::string typed = readLine(tr("common.enter_name"));
    if (!typed.empty()) s.name = typed;

    Menu dm(tr("rules.title"));
    dm.add(difficultyLabel(Difficulty::Easy), "");
    dm.add(difficultyLabel(Difficulty::Normal), "");
    dm.add(difficultyLabel(Difficulty::Hard), "");
    const int d = dm.run();
    if (d >= 0) s.difficulty = d;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// offline match
// ---------------------------------------------------------------------------

SessionResult playOffline(const AppConfig& cfg) {
    const OfflineSetup setup = setupOffline(cfg);
    if (setup.cancelled) return SessionResult::Aborted;

    const uint32_t seed = resolveSeed(cfg.seed);
    const Difficulty diff = difficultyFromInt(setup.difficulty);

    MatchSession ms(cfg.rules, seed);
    GameUI ui;

    int humansLeft = setup.humanSeats;
    int nextHumanSlot = 0;
    for (int seat = 0; seat < cfg.rules.playerCount; ++seat) {
        const int otherHuman = (setup.humanSeats == 2) ? setup.humanB : -1;
        if (humansLeft > 0 && (seat == setup.humanA || seat == otherHuman)) {
            const std::string who = (nextHumanSlot == 0) ? setup.name : setup.name + " #2";
            ms.addController(std::make_unique<HumanController>(who, ui));
            ++nextHumanSlot;
            --humansLeft;
        } else {
            ms.addController(
                std::make_unique<BotController>("CPU " + S(seat + 1), diff, seed + seat, seat));
        }
    }

    // The UI renders whichever seat currently holds the turn; both human seats
    // share this terminal and swap in automatically.
    auto sink = [&ui](int seat, const View& v) {
        ui.setLocalSeat(seat);
        for (const Event& e : v.log) ui.logEvent(e, v);
    };

    ms.startGame();
    const SessionResult r = ms.run(sink, nullptr);
    showMatchResult(ms.game().viewFor(setup.humanA));
    return r;
}

// ---------------------------------------------------------------------------
// main loop
// ---------------------------------------------------------------------------

int runApp(AppConfig& cfg) {
    I18n::instance().setLang(cfg.lang);

    for (;;) {
        Menu menu(tr("menu.title"));
        menu.add(tr("menu.pve"), cfg.rules.describe());
        menu.add(tr("menu.host"), "");
        menu.add(tr("menu.join"), "");
        menu.add(tr("menu.rules"), cfg.rules.describe());
        menu.add(tr("menu.lang"), I18n::instance().isZh() ? "English" : "中文");
        menu.add(tr("menu.quit"), "");

        const int pick = menu.run();
        switch (pick) {
            case 0: playOffline(cfg); break;
            case 1: hostOnlineGame(cfg); break;
            case 2: joinOnlineGame(cfg); break;
            case 3: editRules(cfg.rules, cfg.difficulty, cfg.seed); break;
            case 4:
                cfg.lang = (cfg.lang == Lang::En) ? Lang::Zh : Lang::En;
                I18n::instance().setLang(cfg.lang);
                break;
            case 5:
            default:
                return 0;
        }
    }
}

}  // namespace uno