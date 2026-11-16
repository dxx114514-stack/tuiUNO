# tuiUNO

[![CI](https://github.com/dxx114514-stack/tuiUNO/actions/workflows/ci.yml/badge.svg)](https://github.com/dxx114514-stack/tuiUNO/actions/workflows/ci.yml)

A UNO game for the terminal, written in C++17 with no third-party dependencies.

- **Offline PVE** — 1 or 2 humans against CPU opponents, three AI difficulties.
- **Online PVP** — host a room on any machine, others join over plain TCP.
- **Bilingual** — English and Chinese, switchable from the menu or `--lang`.
- **Full rule set** — the standard 108-card deck, plus optional house rules.

> 中文版见 [README-zh-CN.md](README-zh-CN.md).

---

## Build

Requires CMake ≥ 3.16 and a C++17 compiler. Tested with MinGW-w64 (GCC 14) and
MSVC; the same CMake file builds on Linux with g++ or clang++.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces `build/uno` (the game) and `build/uno_tests` (the test suite).

```sh
ctest --test-dir build --output-on-failure   # run the tests
./build/uno_tests                            # same, but verbose
```

CI builds and runs the suite on Linux (GCC and Clang), macOS (Clang) and Windows
(MSVC) for every push and pull request.

## Play

```sh
./build/uno                       # interactive menu
./build/uno --lang zh             # start in Chinese
./build/uno --players 2 --diff 2  # 2 seats, hard CPU
./build/uno --help                # all options
```

### PVE

Pick **Play vs CPU** from the menu, choose how many seats you want to control
(1 or 2 — with two humans you share the terminal and the UI swaps between you
automatically), then pick a difficulty.

### PVP

The host runs the authoritative game; every client receives its own private view
and submits actions. Only the host's port has to be reachable.

On the host:

```sh
./build/uno --host --port 45231
```

The lobby shows a four-letter room code and a ready-to-paste join command:

```
  room code : K49K
  address   : 192.168.5.20:45231
  join with: uno --join 192.168.5.20 -p 45231 -c K49K
```

On each client:

```sh
./build/uno --join --host-addr 192.168.5.20 --port 45231 --code K49K
```

Empty seats are filled with CPUs, so two players can also play a four-player
game. Press `/` in the lobby or during a match to chat.

Over the internet, forward the port (`netsh interface portproxy` on Windows,
`iptables -A PREROUTING -p tcp --dport 45231 -j DNAT ...` elsewhere) or run both
machines on the same LAN.

## Controls

| Key | Action |
| --- | --- |
| `↑` / `↓`, `W` / `S` | move in menus, move the card cursor |
| `1`–`0` | play the card in that position (wraps past 9) |
| `Enter` | play the highlighted card / confirm |
| `D` | draw a card |
| `P` | pass (only after drawing) |
| `U` | say UNO |
| `C` | catch an opponent on one card who never said UNO |
| `R` `Y` `G` `B` or `1`–`4` | pick a colour after a wild |
| `Y` / `N` | challenge a Wild Draw Four / take the penalty |
| `/` | chat (lobby and match) |
| `Esc`, `Ctrl+C` | leave |

## Rules

The standard 108-card deck: four colours, `0`–`9` (one `0` and two of each
`1`–`9` per colour), Skip / Reverse / Draw Two (two of each per colour), four
Wilds and four Wild Draw Fours.

Everything below is toggleable in **Rule settings**:

| Rule | Default | Notes |
| --- | --- | --- |
| Players | 4 | 2–4 |
| Cards per hand | 7 | 3–10 |
| Target score | 500 | first seat to reach it wins the match |
| Draw Two stacking | off | let the next player answer a `+2` with a `+2` |
| Challenge Wild+4 | on | see below |
| Wild+4 stacking | off | stack a `+4` onto a pending `+4` |
| Must play if able | off | forbid drawing when a legal play exists |
| Recycle discard pile | on | reshuffle once the draw pile empties |
| Penalty for missing UNO | on | the caught player draws two |
| Opening 7-7 hand swap | off | "swap hands" opening |
| Team mode | off | 4 players, seats 0+2 against 1+3 |

**Wild Draw Four.** Playing a `+4` while you hold a card of the colour that was
in effect is a cheat — and like the physical game it is only punished if
somebody challenges. The next player decides whether to call it: if the attacker
really did hold a matching colour, the `+4` is cancelled, the attacker takes
those four cards back and the challenger draws two; otherwise the challenger
pays the whole penalty (which can be more than four when cards are stacked).

**Rounds and scoring.** A round ends the moment a player empties their hand.
Everyone else scores the face value of the cards they are still holding
(number cards at face value, action cards 20, wilds 50). In team mode only the
losing side's cards are counted and the score goes to the winning pair. The
match ends when someone reaches the target score.

**Card conservation.** The engine never invents or loses cards. When the draw
pile and the discard pile both run dry, a player who must draw simply draws
nothing and the turn passes — the 107 cards that can leave the draw pile are all
accounted for.

## CPU opponents

All three difficulties share the same card counting: the bot tracks its own
hand, everything it has seen played, and everything left in the draw pile, so it
can estimate how many cards of each colour any opponent is holding.

- **Easy** — plays a random legal card, picks a colour at random, forgets to say
  UNO about half the time, and sometimes draws even when it could play.
- **Normal** — scores every legal card: values disruption, avoids feeding an
  opponent who is close to going out, dumps expensive cards, picks the colour
  that hurts the next player most, stacks `+2`s when the next player is unlikely
  to counter, and challenges a `+4` when the attacker plausibly cheated.
- **Hard** — additionally reasons about the draw pile running low, keeps a card
  of the colour the next player is strongest in, and plays `+4` only when it
  holds no card of the active colour.

Any CPU seat can be filled by a human in a PVP lobby; a human who disconnects
mid-match is replaced by a CPU so the match keeps running.

## Layout

```
include/uno/
  card.hpp        card model, colours, kinds, scoring values
  deck.hpp        draw pile, discard pile, recycling
  rules.hpp       the tunable rule set
  game.hpp        the rules engine and the per-player View projection
  serialize.hpp   little-endian binary codec for Views
  protocol.hpp    message framing and payload helpers
  net.hpp         non-blocking TCP (Winsock2 / BSD sockets)
  ai.hpp          CPU opponents
  console.hpp     UTF-8, ANSI colours, raw key input
  ui.hpp          board, hands, menus, prompts
  player.hpp      seat controllers (human / CPU / remote)
  session.hpp     match loop
  app.hpp         entry points
src/              implementations plus main.cpp, app.cpp, online.cpp
tests/            self-contained test suite
```

`Game` is pure logic: it takes actions and produces a `View` per player, and it
never touches the terminal or the network. That is what lets the same engine
drive a local UI, a CPU, and a remote client, and it is why `View` carries a
player's own hand only — a snapshot is always safe to send over the wire.

### Networking

Frames are `[u8 type][u32 length][payload]`. The host owns the state and pushes a
fresh snapshot after every transition; clients only ever send actions and chat.
Every socket is non-blocking and the whole thing runs on one thread, so the host
can also play its own seat while serving the others. A remote player who goes
quiet for 30 seconds is played for, rather than stalling the table.

A connection must join the room within five seconds or its seat is released, and
the room code is checked before that seat is handed out. Inbound nicknames and
chat are stripped of control bytes (including ANSI escapes) and length-capped.
Outbound frames are queued whole per peer: a slow reader delays later frames but
can never split one, and a peer that falls too far behind is disconnected. Frame
lengths are capped as well, so a desynchronised stream cannot turn into a memory
DoS.

## Tests

`tests/test_uno.cpp` runs about eleven million assertions in about a second and
covers:

- the 108-card deck composition and recycling, including full exhaustion
- play legality, out-of-turn rejection, and the five phases of the turn machine
- `+2` stacking, both `+4` challenge outcomes, and the catch-UNO window
- complete matches at every difficulty and seat count, asserting that all 108
  cards stay accounted for and that no bot ever submits an illegal action
- `View` round-tripping through the wire format, including per-player privacy
- frame decoding, including byte-at-a-time delivery, truncated frames, and
  oversized length prefixes that must be rejected
- room-code validation (a wrong code must be rejected), outbound buffering that
  never splits a frame, and sanitisation of control bytes in nicknames/chat
- rendering the real board for every seat, CJK column widths, and i18n coverage
  that fails if any message is ever rendered with an unfilled `{}` placeholder

## Licence

MIT — see [LICENSE](LICENSE). Written from scratch; the UNO card names and rules
are trademarks of Mattel and are used here only to identify the game being
implemented.
