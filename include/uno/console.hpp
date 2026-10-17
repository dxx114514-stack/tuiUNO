// Terminal plumbing: UTF-8 output, ANSI colours, raw key input on Windows and
// POSIX. Everything above this layer only deals with KeyEvent and std::string.
#pragma once

#include <cstdint>
#include <string>

namespace uno {

enum class Key : uint8_t {
    None, Char, Up, Down, Left, Right, Enter, Space, Backspace, Escape,
    Tab, PageUp, PageDown, Home, End, CtrlC,
};

struct KeyEvent {
    Key key = Key::None;
    char ch = 0;  // valid when key == Key::Char

    bool isChar(char c) const { return key == Key::Char && ch == c; }
    bool isCharIgnoreCase(char c) const {
        return key == Key::Char && (ch == c || ch == static_cast<char>(::toupper(
                                       static_cast<unsigned char>(c))));
    }
};

// Puts the terminal into UTF-8 + ANSI + raw-input mode.
void consoleInit();
// Restores the previous terminal state.
void consoleReset();
// False when stdin is a pipe or a file, i.e. no keystrokes can ever arrive.
// Callers that would otherwise block forever should bail out when this is false.
bool consoleInteractive();

int consoleWidth();
int consoleHeight();

void clearScreen();
void moveTo(int row, int col);
void hideCursor();
void showCursor();
void setColor(const char* code);   // e.g. "\x1b[31m"
void setColorRaw(int r, int g, int b);
void resetColor();

void print(const std::string& s);
void println(const std::string& s);
void printfLine(const char* fmt, ...);

inline void repeat(const std::string& s, int n) {
    for (int i = 0; i < n; ++i) print(s);
}

// Non-blocking key read. Returns false when nothing arrived in time.
bool pollKey(int timeoutMs, KeyEvent& out);
// Blocks until a key arrives.
KeyEvent waitKey();

// Single-line text entry with echo and backspace.
std::string readLine(const std::string& prompt);

}  // namespace uno