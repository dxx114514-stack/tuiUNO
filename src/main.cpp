// tuiUNO - a terminal UNO game with offline AI play and online PVP.
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "uno/app.hpp"
#include "uno/console.hpp"
#include "uno/i18n.hpp"
#include "uno/net.hpp"

namespace {

void printUsage() {
    std::printf("tuiUNO - UNO for the terminal\n\n");
    std::printf("Usage: uno [options]\n");
    std::printf("  --host              create an online room and wait for players\n");
    std::printf("  --join              join an online room\n");
    std::printf("  --host-addr <ip>    host address to join (default 127.0.0.1)\n");
    std::printf("  --port <n>          TCP port (default 45231)\n");
    std::printf("  --code <code>       room code, skips the prompt when joining\n");
    std::printf("  --name <name>       player name\n");
    std::printf("  --players <2-4>     seat count\n");
    std::printf("  --seed <n>          random seed (0 = random)\n");
    std::printf("  --lang <en|zh>      interface language\n");
    std::printf("  --diff <0-2>        CPU difficulty: 0 easy, 1 normal, 2 hard\n");
    std::printf("  --help              this text\n");
}

bool wantFlag(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "C");
    uno::consoleInit();
    uno::net::init();

    if (wantFlag(argc, argv, "--help") || wantFlag(argc, argv, "-h")) {
        printUsage();
        uno::consoleReset();
        return 0;
    }

    uno::AppConfig cfg;
    cfg.rules.playerCount = 4;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const bool hasValue = (i + 1 < argc);
        const std::string value = hasValue ? argv[i + 1] : std::string();

        if (flag == "--host-addr" && hasValue) { cfg.joinHost = value; ++i; }
        else if (flag == "--code" && hasValue) { cfg.joinCode = value; ++i; }
        else if (flag == "--name" && hasValue) { cfg.playerName = value; ++i; }
        else if (flag == "--port" && hasValue) {
            const long p = std::strtol(value.c_str(), nullptr, 10);
            if (p > 0 && p < 65536) cfg.port = static_cast<uint16_t>(p);
            ++i;
        } else if (flag == "--players" && hasValue) {
            const int n = std::atoi(value.c_str());
            if (n >= 2 && n <= 4) cfg.rules.playerCount = n;
            ++i;
        } else if (flag == "--seed" && hasValue) {
            cfg.seed = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
            ++i;
        } else if (flag == "--diff" && hasValue) {
            const int d = std::atoi(value.c_str());
            cfg.difficulty = (d >= 0 && d <= 2) ? d : 1;
            ++i;
        } else if (flag == "--lang" && hasValue) {
            cfg.lang = uno::parseLang(value);
            ++i;
        }
    }

    uno::I18n::instance().setLang(cfg.lang);

    if (wantFlag(argc, argv, "--join")) {
        uno::joinOnlineGame(cfg);
    } else if (wantFlag(argc, argv, "--host")) {
        uno::hostOnlineGame(cfg);
    } else {
        uno::runApp(cfg);
    }

    uno::consoleReset();
    return 0;
}