#pragma once

#include <cstdint>
#include <string>

#include "uno/game.hpp"
#include "uno/i18n.hpp"
#include "uno/session.hpp"

namespace uno {

struct AppConfig {
    Rules rules;
    Lang lang = Lang::En;
    int difficulty = 1;   // 0 easy, 1 normal, 2 hard
    uint32_t seed = 0;    // 0 = random

    // Pre-filled answers. When empty the corresponding prompt is shown.
    std::string playerName;
    std::string joinHost;   // address to reach when joining
    std::string joinCode;   // room code to join
    uint16_t port = 0;      // 0 = prompt
};

int runApp(AppConfig& cfg);
SessionResult playOffline(const AppConfig& cfg);

// Blocking, full-screen online flows.
void hostOnlineGame(const AppConfig& cfg);
void joinOnlineGame(const AppConfig& cfg);

}  // namespace uno