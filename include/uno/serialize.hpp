// Compact binary codec shared by the client and the host. Little-endian,
// length-prefixed strings. No dependencies, no reflection.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uno/game.hpp"
#include "uno/rules.hpp"

namespace uno {

class ByteWriter {
  public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void i32(int32_t v);
    void str(const std::string& s);
    void bytes(const void* p, size_t n);
    void card(const Card& c);
    void rules(const Rules& r);
    void action(const Action& a);
    void playerInfo(const PlayerInfo& p);
    void event(const Event& e);

    const std::string& data() const { return buf_; }
    std::string take() { return std::move(buf_); }
    size_t size() const { return buf_.size(); }

  private:
    std::string buf_;
};

class ByteReader {
  public:
    ByteReader(const char* data, size_t len) : p_(data), end_(data + len) {}
    explicit ByteReader(const std::string& s) : p_(s.data()), end_(s.data() + s.size()) {}

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    int32_t i32();
    std::string str();
    void bytes(void* dst, size_t n);
    Card card();
    Rules rules();
    Action action();
    PlayerInfo playerInfo();
    Event event();

    bool ok() const { return ok_; }
    bool empty() const { return p_ >= end_; }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }

  private:
    bool need(size_t n) {
        if (ok_ && static_cast<size_t>(end_ - p_) >= n) return true;
        ok_ = false;
        return false;
    }
    const char* p_;
    const char* end_;
    bool ok_ = true;
};

void writeView(ByteWriter& w, const View& v);
bool readView(ByteReader& r, View& v);

}  // namespace uno