// Little-endian byte buffer writer/reader for the coop protocol (portable).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "utf8.h"

namespace net {

class Writer {
public:
    std::vector<uint8_t> data;

    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void u64(uint64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void f64(double v) { raw(&v, 8); }
    void raw(const void* p, size_t n)
    {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        data.insert(data.end(), b, b + n);
    }
    // a longer text is cut between two characters, never inside one (BUG-0164)
    void str8(const std::string& s)
    {
        size_t n = utf8::g_cut_fix ? utf8::cut_bytes(s, 255) : (s.size() > 255 ? 255 : s.size());
        u8((uint8_t)n);
        raw(s.data(), n);
    }
    void str16(const std::string& s)
    {
        size_t n = utf8::g_cut_fix ? utf8::cut_bytes(s, 4000) : (s.size() > 4000 ? 4000 : s.size());
        u16((uint16_t)n);
        raw(s.data(), n);
    }
    size_t size() const { return data.size(); }
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool ok() const { return ok_; }
    size_t left() const { return ok_ ? n_ - pos_ : 0; }
    const uint8_t* cur() const { return p_ + pos_; }

    uint8_t u8()
    {
        uint8_t v = 0;
        raw(&v, 1);
        return v;
    }
    uint16_t u16()
    {
        uint16_t v = 0;
        raw(&v, 2);
        return v;
    }
    uint32_t u32()
    {
        uint32_t v = 0;
        raw(&v, 4);
        return v;
    }
    uint64_t u64()
    {
        uint64_t v = 0;
        raw(&v, 8);
        return v;
    }
    float f32()
    {
        float v = 0;
        raw(&v, 4);
        return v;
    }
    double f64()
    {
        double v = 0;
        raw(&v, 8);
        return v;
    }
    bool raw(void* out, size_t n)
    {
        if (!ok_ || pos_ + n > n_) {
            ok_ = false;
            return false;
        }
        if (n) // an empty read may pass nullptr (an empty vector's data()): memcpy must not see it
            memcpy(out, p_ + pos_, n);
        pos_ += n;
        return true;
    }
    bool skip(size_t n)
    {
        if (!ok_ || pos_ + n > n_) {
            ok_ = false;
            return false;
        }
        pos_ += n;
        return true;
    }
    std::string str8()
    {
        size_t n = u8();
        std::string s;
        if (n && ok_ && pos_ + n <= n_) {
            s.assign(reinterpret_cast<const char*>(p_ + pos_), n);
            pos_ += n;
        } else if (n)
            ok_ = false;
        return s;
    }
    std::string str16()
    {
        size_t n = u16();
        std::string s;
        if (n && ok_ && pos_ + n <= n_) {
            s.assign(reinterpret_cast<const char*>(p_ + pos_), n);
            pos_ += n;
        } else if (n)
            ok_ = false;
        return s;
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace net
