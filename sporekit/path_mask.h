// Private paths in log and report text (BUG-1124, A1 of plan 0.7.15). Portable: no Windows headers,
// unit tested on the host (tests/path_mask_tests.cpp); sporekit/log.cpp fills it at log_init.
//
// The text is UTF-8. A path is compared without case like Windows does: ASCII, Latin-1, Latin
// Extended-A, Greek and Cyrillic letters are folded, '/' counts as '\'. Bytes that are no valid
// UTF-8 (a path in the ANSI code page) are compared as they are, so the ANSI form of a path can be
// a mask of its own. The longest path wins (the game folder inside the profile is <game>).
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sk::pathmask {

// one code point at s[i] (n: its byte count); an invalid sequence is its first byte, marked
inline uint32_t decode(const std::string& s, size_t i, size_t& n)
{
    auto b = [&](size_t k) { return (uint32_t)(unsigned char)s[i + k]; };
    uint32_t c = b(0);
    n = 1;
    auto cont = [&](size_t k) { return i + k < s.size() && (b(k) & 0xC0) == 0x80; };
    if (c < 0x80)
        return c;
    if (c >= 0xC2 && c <= 0xDF && cont(1)) {
        n = 2;
        return ((c & 0x1F) << 6) | (b(1) & 0x3F);
    }
    if (c >= 0xE0 && c <= 0xEF && cont(1) && cont(2)) {
        uint32_t v = ((c & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F);
        if (v >= 0x800) {
            n = 3;
            return v;
        }
    }
    if (c >= 0xF0 && c <= 0xF4 && cont(1) && cont(2) && cont(3)) {
        uint32_t v = ((c & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F);
        if (v >= 0x10000 && v <= 0x10FFFF) {
            n = 4;
            return v;
        }
    }
    return 0x80000000u | c; // not UTF-8: the byte itself (never equal to a real code point)
}

// the lower case of the letters a user or folder name is usually written in
inline uint32_t fold(uint32_t c)
{
    if (c == '/')
        return '\\';
    if (c < 0x80)
        return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) // Latin-1: À..Þ
        return c + 0x20;
    if (c == 0x178) // Ÿ
        return 0xFF;
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) // Latin Extended-A, even = upper
        return c | 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) // odd = upper
        return (c & 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) // Greek capitals
        return c + 0x20;
    if (c == 0x386)
        return 0x3AC;
    if (c >= 0x388 && c <= 0x38A)
        return c + 0x25;
    if (c == 0x38C)
        return 0x3CC;
    if (c == 0x38E || c == 0x38F)
        return c + 0x3F;
    if (c >= 0x400 && c <= 0x40F) // Cyrillic Ѐ..Џ
        return c + 0x50;
    if (c >= 0x410 && c <= 0x42F) // Cyrillic А..Я
        return c + 0x20;
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) // even = upper
        return c | 1;
    return c;
}

// the byte count of `path` matched at s[i] (case folded), 0 when it does not match there
inline size_t match_at(const std::string& s, size_t i, const std::string& path)
{
    size_t a = i, p = 0;
    while (p < path.size()) {
        if (a >= s.size())
            return 0;
        size_t na, np;
        uint32_t ca = decode(s, a, na), cp = decode(path, p, np);
        if (ca != cp && fold(ca) != fold(cp))
            return 0;
        a += na;
        p += np;
    }
    return a - i;
}

class Masks {
public:
    // a folder that is private (its trailing slashes do not count); too short ones ("C:", "Z:\") are no path
    void add(std::string path, const std::string& token)
    {
        while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
            path.pop_back();
        if (path.size() < 4)
            return;
        for (auto& [p, t] : v_)
            if (match_at(p, 0, path) == p.size() && p.size() == path.size())
                return; // the same folder once
        v_.push_back({path, token});
        std::stable_sort(v_.begin(), v_.end(),
                         [](const auto& x, const auto& y) { return x.first.size() > y.first.size(); });
    }
    // true when `path` is the folder of an earlier mask or inside it
    bool covered(const std::string& path) const
    {
        for (auto& [p, t] : v_) {
            size_t n = match_at(path, 0, p);
            if (n && (n == path.size() || path[n] == '\\' || path[n] == '/'))
                return true;
        }
        return false;
    }
    void clear() { v_.clear(); }
    bool empty() const { return v_.empty(); }
    size_t size() const { return v_.size(); }

    std::string apply(const std::string& s) const
    {
        if (v_.empty() || s.size() < 4)
            return s;
        std::string out;
        bool changed = false;
        size_t i = 0;
        while (i < s.size()) {
            size_t hit = 0;
            const std::string* token = nullptr;
            for (auto& [p, t] : v_)
                if ((hit = match_at(s, i, p)) != 0) {
                    token = &t;
                    break;
                }
            if (token) {
                if (!changed)
                    out.assign(s, 0, i);
                out += *token;
                i += hit;
                changed = true;
                continue;
            }
            size_t n;
            decode(s, i, n); // a code point at a time: a mask never starts inside a letter
            if (changed)
                out.append(s, i, n);
            i += n;
        }
        return changed ? out : s;
    }

private:
    std::vector<std::pair<std::string, std::string>> v_; // longest first
};

} // namespace sk::pathmask
