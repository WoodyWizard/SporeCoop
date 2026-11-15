// UTF-8 helpers for names, chat and overlay text (portable, tests/net_tests.cpp).
// Text in the mod is UTF-8; byte counts are not characters (BUG-0129, BUG-0159, BUG-0164).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace utf8 {

inline bool g_cut_fix = true; // false: net::Writer cuts by bytes as before BUG-0164 (text_tests --old)

inline bool cont(unsigned char c) { return (c & 0xC0) == 0x80; }

// the largest n <= max that does not end inside a UTF-8 sequence
inline size_t cut_bytes(const std::string& s, size_t max)
{
    if (s.size() <= max)
        return s.size();
    size_t n = max;
    while (n > 0 && cont((unsigned char)s[n]))
        n--;
    return n;
}

inline size_t count(const std::string& s)
{
    size_t n = 0;
    for (unsigned char c : s)
        n += !cont(c);
    return n;
}

// the first n code points
inline std::string head(const std::string& s, size_t n)
{
    size_t i = 0, k = 0;
    while (i < s.size()) {
        if (!cont((unsigned char)s[i])) {
            if (k == n)
                break;
            k++;
        }
        i++;
    }
    return s.substr(0, i);
}

// the code point at s[i] (a lead byte); i moves past it. Bad bytes give U+FFFD.
inline uint32_t next(const std::string& s, size_t& i)
{
    unsigned char c = (unsigned char)s[i++];
    int more = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (more < 0)
        return 0xFFFD;
    uint32_t cp = more == 0 ? c : more == 1 ? (c & 0x1F) : more == 2 ? (c & 0x0F) : (c & 0x07);
    for (int k = 0; k < more; k++) {
        if (i >= s.size() || !cont((unsigned char)s[i]))
            return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    return cp;
}

inline bool valid(const std::string& s)
{
    size_t i = 0;
    while (i < s.size()) {
        if (cont((unsigned char)s[i]))
            return false;
        size_t at = i;
        uint32_t cp = next(s, i);
        if (cp == 0xFFFD && !(i - at == 3 && (unsigned char)s[at] == 0xEF))
            return false;
    }
    return true;
}

// East Asian wide characters take about two Latin letters of the overlay font
inline bool wide(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || cp >= 0x20000;
}

// the width in letter cells: one per code point, two for a wide one
inline int columns(const std::string& s)
{
    int n = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (cont((unsigned char)s[i])) {
            i++;
            continue;
        }
        n += wide(next(s, i)) ? 2 : 1;
    }
    return n;
}

// the overlay font's width estimate (Arial 18 bold: about 9 px per letter cell)
inline int overlay_width(const std::string& s) { return columns(s) * 9; }

// a player name: at most kNameMax code points (the co-op window's limit), no control characters
constexpr size_t kNameMax = 24;
inline std::string player_name(const std::string& in)
{
    std::string s;
    for (unsigned char c : in)
        if (c >= 0x20 && c != 0x7F)
            s += (char)c;
    return head(s, kNameMax);
}

// BUG-0774: text from another machine (a player name, a LAN game's info line, a planet name) that goes into logs,
// the co-op window and files: one line of valid UTF-8, at most `max_cp` code points. Dropped: bytes that are not
// UTF-8 (stray continuation bytes, cut or overlong sequences, surrogates), C0 / C1 control characters, DEL and the
// Unicode line and paragraph separators. Everything else (letters of any script, emoji) stays as it came.
inline std::string clean_text(const std::string& in, size_t max_cp)
{
    std::string s;
    size_t i = 0, n = 0;
    while (i < in.size() && n < max_cp) {
        size_t at = i;
        uint32_t cp = next(in, i);
        size_t len = i - at;
        bool bad = (cp == 0xFFFD && !(len == 3 && (unsigned char)in[at] == 0xEF)) || (len == 2 && cp < 0x80) ||
                   (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF);
        if (bad || cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x2028 || cp == 0x2029)
            continue;
        s.append(in, at, len);
        n++;
    }
    return s;
}
inline std::string net_name(const std::string& in) { return clean_text(in, kNameMax); }

} // namespace utf8
