#include "uno/console.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#if UNO_WIN
#include <conio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace uno {

namespace {

#if UNO_WIN
HANDLE g_hIn = nullptr;
HANDLE g_hOut = nullptr;
DWORD g_oldInMode = 0;
DWORD g_oldOutMode = 0;
bool g_saved = false;
bool g_interactive = true;
bool g_outIsConsole = true;
UINT g_oldOutCp = 0;
UINT g_oldInCp = 0;
#else
termios g_savedTerm{};
bool g_termSaved = false;
#endif

}  // namespace

// ---------------------------------------------------------------------------
// setup / teardown
// ---------------------------------------------------------------------------

void consoleInit() {
#if UNO_WIN
    if (g_saved) return;
    g_hIn = GetStdHandle(STD_INPUT_HANDLE);
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_oldOutCp = GetConsoleOutputCP();
    g_oldInCp = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    DWORD mode = 0;
    if (g_hOut && GetConsoleMode(g_hOut, &mode)) {
        g_outIsConsole = true;
        g_oldOutMode = mode;
        SetConsoleMode(g_hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    } else {
        g_outIsConsole = false;
        // Writing through the CRT would translate to the system ANSI code page.
        // Binary mode passes our UTF-8 bytes through untouched.
        ::_setmode(::_fileno(stdout), _O_BINARY);
        ::_setmode(::_fileno(stderr), _O_BINARY);
    }
    if (GetConsoleMode(g_hIn, &mode)) {
        g_oldInMode = mode;
        g_interactive = true;
        // Disable echo/line editing: we render every keystroke ourselves.
        SetConsoleMode(g_hIn, (mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT)) |
                                 ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    } else {
        // stdin is redirected (a pipe or a file): there are no keystrokes to
        // wait for, so the UI must not block forever.
        g_interactive = false;
    }
    g_saved = true;
#else
    if (g_termSaved) return;
    if (tcgetattr(STDIN_FILENO, &g_savedTerm) == 0) {
        termios raw = g_savedTerm;
        raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(unsigned)(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_termSaved = true;
    }
#endif
}

void consoleReset() {
#if UNO_WIN
    if (!g_saved) return;
    if (g_hOut) SetConsoleMode(g_hOut, g_oldOutMode);
    if (g_hIn) SetConsoleMode(g_hIn, g_oldInMode);
    SetConsoleOutputCP(g_oldOutCp);
    SetConsoleCP(g_oldInCp);
    showCursor();
    resetColor();
    g_saved = false;
#else
    if (g_termSaved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
        g_termSaved = false;
    }
#endif
}

bool consoleInteractive() {
#if UNO_WIN
    return g_interactive;
#else
    return true;
#endif
}

// ---------------------------------------------------------------------------
// geometry and output
// ---------------------------------------------------------------------------

int consoleWidth() {
#if UNO_WIN
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (g_hOut && GetConsoleScreenBufferInfo(g_hOut, &csbi)) {
        const int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return w > 20 ? w : 80;
    }
    return 80;
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
#endif
}

int consoleHeight() {
#if UNO_WIN
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (g_hOut && GetConsoleScreenBufferInfo(g_hOut, &csbi)) {
        const int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        return h > 10 ? h : 24;
    }
    return 24;
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return 24;
#endif
}

void clearScreen() { print("\x1b[2J\x1b[H"); }
void moveTo(int row, int col) { printf("\x1b[%d;%dH", row, col); }
void hideCursor() { print("\x1b[?25l"); }
void showCursor() { print("\x1b[?25h"); }
void setColor(const char* code) { print(code); }
void resetColor() { print("\x1b[0m"); }

void setColorRaw(int r, int g, int b) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
    print(buf);
}

void print(const std::string& s) {
#if UNO_WIN
    if (g_hOut && g_outIsConsole) {
        ::WriteConsoleA(g_hOut, s.data(), static_cast<DWORD>(s.size()), nullptr, nullptr);
        return;
    }
    // stdout is redirected to a file or a pipe: go through the CRT instead.
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
#else
    const ssize_t n = ::write(STDOUT_FILENO, s.data(), s.size());
    (void)n;
#endif
}

void println(const std::string& s) { print(s + "\r\n"); }

void printfLine(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(std::string(buf) + "\r\n");
}

// ---------------------------------------------------------------------------
// key input
// ---------------------------------------------------------------------------

#if UNO_WIN

namespace {
KeyEvent mapVKey(WORD vk, wchar_t uc) {
    KeyEvent e;
    switch (vk) {
        case VK_UP: e.key = Key::Up; return e;
        case VK_DOWN: e.key = Key::Down; return e;
        case VK_LEFT: e.key = Key::Left; return e;
        case VK_RIGHT: e.key = Key::Right; return e;
        case VK_RETURN: e.key = Key::Enter; return e;
        case VK_SPACE: e.key = Key::Char; e.ch = ' '; return e;
        case VK_BACK: e.key = Key::Backspace; return e;
        case VK_ESCAPE: e.key = Key::Escape; return e;
        case VK_TAB: e.key = Key::Tab; return e;
        case VK_PRIOR: e.key = Key::PageUp; return e;
        case VK_NEXT: e.key = Key::PageDown; return e;
        case VK_HOME: e.key = Key::Home; return e;
        case VK_END: e.key = Key::End; return e;
        case VK_CONTROL: e.key = Key::None; return e;
        case 'C': {
            const DWORD st = GetKeyState(VK_CONTROL);
            if (st & 0x8000) { e.key = Key::CtrlC; return e; }
            break;
        }
        default: break;
    }
    if (uc >= 0x20 && uc < 0x7f) {
        e.key = Key::Char;
        e.ch = static_cast<char>(uc);
        return e;
    }
    e.key = Key::None;
    return e;
}

bool readWindowsKey(int timeoutMs, KeyEvent& out) {
    // 1) Already-queued console input records.
    DWORD avail = 0;
    if (g_hIn && GetNumberOfConsoleInputEvents(g_hIn, &avail) && avail > 0) {
        INPUT_RECORD rec;
        DWORD got = 0;
        if (ReadConsoleInputW(g_hIn, &rec, 1, &got) && got > 0 &&
            rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            KeyEvent e = mapVKey(rec.Event.KeyEvent.wVirtualKeyCode,
                                 rec.Event.KeyEvent.uChar.UnicodeChar);
            if (e.key != Key::None) { out = e; return true; }
        }
        return false;
    }
    // 2) Fall back to _kbhit for redirected input.
    if (_kbhit()) {
        const int c = _getch();
        if (c == 0 || c == 0xe0) {
            const int ext = _getch();
            KeyEvent e;
            switch (ext) {
                case 72: e.key = Key::Up; break;
                case 80: e.key = Key::Down; break;
                case 75: e.key = Key::Left; break;
                case 77: e.key = Key::Right; break;
                default: break;
            }
            if (e.key != Key::None) { out = e; return true; }
            return false;
        }
        if (c == 3) { out.key = Key::CtrlC; return true; }
        KeyEvent e;
        e.key = Key::Char;
        e.ch = static_cast<char>(c);
        out = e;
        return true;
    }
    if (timeoutMs > 0) Sleep(static_cast<DWORD>(timeoutMs < 20 ? 1 : timeoutMs / 10));
    return false;
}

}  // namespace

bool pollKey(int timeoutMs, KeyEvent& out) {
    const int steps = timeoutMs <= 0 ? 1 : (timeoutMs + 9) / 10;
    for (int i = 0; i < steps; ++i) {
        if (readWindowsKey(i == 0 ? 0 : 10, out)) return true;
    }
    return false;
}

#else

namespace {

bool readByteTimeout(int timeoutMs, unsigned char& c) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    const int n = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &tv);
    if (n <= 0) return false;
    unsigned char buf[1];
    if (::read(STDIN_FILENO, buf, 1) != 1) return false;
    c = buf[0];
    return true;
}

KeyEvent mapEscapeSequence(std::vector<unsigned char>& pending) {
    KeyEvent e;
    if (pending.empty()) return e;
    if (pending[0] != 0x1b) {
        e.key = Key::Char;
        e.ch = static_cast<char>(pending[0]);
        pending.erase(pending.begin());
        return e;
    }
    if (pending.size() == 1) {
        e.key = Key::Escape;
        pending.clear();
        return e;
    }
    if (pending[1] != '[' && pending[1] != 'O') {
        e.key = Key::Escape;
        pending.erase(pending.begin());
        return e;
    }
    // Wait briefly for the rest of the sequence.
    for (int i = 0; i < 12 && pending.size() < 4; ++i) {
        unsigned char c;
        if (!readByteTimeout(10, c)) break;
        pending.push_back(c);
    }
    const char code = pending.size() > 2 ? static_cast<char>(pending[2]) : '\0';
    switch (code) {
        case 'A': e.key = Key::Up; break;
        case 'B': e.key = Key::Down; break;
        case 'C': e.key = Key::Right; break;
        case 'D': e.key = Key::Left; break;
        case 'H': e.key = Key::Home; break;
        case 'F': e.key = Key::End; break;
        case '5': e.key = Key::PageUp; break;
        case '6': e.key = Key::PageDown; break;
        default: e.key = Key::Escape; break;
    }
    pending.clear();
    return e;
}

std::vector<unsigned char> g_pending;

bool readPosixKey(int timeoutMs, KeyEvent& out) {
    if (!g_pending.empty()) {
        out = mapEscapeSequence(g_pending);
        if (out.key != Key::None) return true;
    }
    unsigned char c;
    if (!readByteTimeout(timeoutMs, c)) return false;
    g_pending.push_back(c);
    out = mapEscapeSequence(g_pending);
    return out.key != Key::None;
}

}  // namespace

bool pollKey(int timeoutMs, KeyEvent& out) { return readPosixKey(timeoutMs, out); }

#endif

KeyEvent waitKey() {
    KeyEvent e;
#if UNO_WIN
    if (!g_interactive) {
        // No console on stdin: behave as if the user pressed Escape so menus
        // unwind instead of spinning.
        e.key = Key::Escape;
        return e;
    }
#endif
    while (!pollKey(50, e)) {
    }
    return e;
}

std::string readLine(const std::string& prompt) {
    print(prompt);
#if UNO_WIN
    if (!g_interactive) {
        println("");
        return {};
    }
#endif
    std::string buf;
    while (true) {
        KeyEvent e = waitKey();
        if (e.key == Key::Enter) break;
        if (e.key == Key::CtrlC || e.key == Key::Escape) {
            buf.clear();
            break;
        }
        if (e.key == Key::Backspace) {
            if (!buf.empty()) {
                // Erase the last cell plus anything it occupied.
                size_t back = 1;
                while (back < buf.size() && (static_cast<unsigned char>(buf[buf.size() - back]) & 0xc0) == 0x80) {
                    ++back;
                }
                buf.erase(buf.size() - back);
                print("\x1b[D\x1b[K");
                for (size_t i = 0; i < back; ++i) print("\x1b[D");
                continue;
            }
            continue;
        }
        if (e.key == Key::Char && e.ch >= 0x20 && e.ch < 0x7f) {
            buf.push_back(e.ch);
            print(std::string(1, e.ch));
        }
    }
    println("");
    return buf;
}

}  // namespace uno