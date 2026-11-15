// The co-op window's text line typed by the mod while the game holds the input lock (BUG-0482, BUG-1027):
// characters the game window received (UTF-16 units of WM_CHAR) applied to the line's UTF-8 text.
// Portable (tests/text_tests.cpp).
#pragma once

#include <cstddef>
#include <string>

namespace typed_text {

// BUG-1027: the limit is the edit's own (UTF-16 units, IEdit max length): the 0.7.13 code compared it with the UTF-8
// byte count (a Cyrillic line stopped at half its limit, CJK at a third), took DEL (0x7F) and encoded each half of a
// surrogate pair on its own (an emoji became two U+FFFD). Off (`coop.fix core.ui_locked_edit_utf16 off`): as before.
inline bool g_utf16_fix = true;

inline std::u16string to_u16(const std::string& s)
{
    std::u16string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int more = c < 0x80 ? 0 : c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
        if (more < 0 || i + more >= s.size() + (more ? 0 : 1)) {
            r += u'�';
            i++;
            continue;
        }
        char32_t cp = more == 0 ? c : more == 1 ? (c & 0x1F) : more == 2 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k <= more; k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += more + 1;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            r += (char16_t)(0xD800 + (cp >> 10));
            r += (char16_t)(0xDC00 + (cp & 0x3FF));
        } else
            r += (char16_t)cp;
    }
    return r;
}

inline std::string to_utf8(const std::u16string& u)
{
    std::string r;
    for (size_t i = 0; i < u.size(); i++) {
        char32_t cp = u[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (u[++i] - 0xDC00);
        else if (cp >= 0xD800 && cp <= 0xDFFF)
            cp = 0xFFFD; // half a pair
        if (cp < 0x80)
            r += (char)cp;
        else if (cp < 0x800) {
            r += (char)(0xC0 | (cp >> 6));
            r += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            r += (char)(0xE0 | (cp >> 12));
            r += (char)(0x80 | ((cp >> 6) & 0x3F));
            r += (char)(0x80 | (cp & 0x3F));
        } else {
            r += (char)(0xF0 | (cp >> 18));
            r += (char)(0x80 | ((cp >> 12) & 0x3F));
            r += (char)(0x80 | ((cp >> 6) & 0x3F));
            r += (char)(0x80 | (cp & 0x3F));
        }
    }
    return r;
}

// the 0.7.13 code (locked_edit_apply before BUG-1027)
inline std::string apply_old(std::string t, const std::u16string& q, int max_len, bool& enter, unsigned& typed)
{
    for (char16_t c : q) {
        if (c == 0x0D) {
            enter = true;
            break;
        }
        if (c == 0x08) {
            while (!t.empty() && (t.back() & 0xC0) == 0x80) // a UTF-8 continuation byte
                t.pop_back();
            if (!t.empty())
                t.pop_back();
        } else if (c >= 0x20 && (int)t.size() < max_len) {
            char b[4];
            int n = c < 0x80 ? (b[0] = (char)c, 1)
                    : c < 0x800 ? (b[0] = (char)(0xC0 | (c >> 6)), b[1] = (char)(0x80 | (c & 0x3F)), 2)
                                : (b[0] = (char)(0xE0 | (c >> 12)), b[1] = (char)(0x80 | ((c >> 6) & 0x3F)),
                                   b[2] = (char)(0x80 | (c & 0x3F)), 3);
            t.append(b, n);
            typed++;
        }
    }
    return t;
}

// the typed characters `q` (UTF-16 units: Backspace 0x08, Enter 0x0D ends and sets `enter`) applied to the line's
// text `t` (UTF-8) of at most `max_len` UTF-16 units; `typed` counts the characters taken
inline std::string apply(const std::string& t, const std::u16string& q, int max_len, bool& enter, unsigned& typed)
{
    if (!g_utf16_fix)
        return apply_old(t, q, max_len, enter, typed);
    std::u16string u = to_u16(t);
    for (size_t i = 0; i < q.size(); i++) {
        char16_t c = q[i];
        if (c == 0x0D) {
            enter = true;
            break;
        }
        if (c == 0x08) {
            if (!u.empty() && u.back() >= 0xDC00 && u.back() <= 0xDFFF && u.size() >= 2 && u[u.size() - 2] >= 0xD800 &&
                u[u.size() - 2] <= 0xDBFF)
                u.pop_back(); // the whole pair
            if (!u.empty())
                u.pop_back();
            continue;
        }
        if (c < 0x20 || c == 0x7F || (c >= 0xDC00 && c <= 0xDFFF)) // controls, DEL, a stray low half
            continue;
        if (c >= 0xD800 && c <= 0xDBFF) { // a pair: both halves or nothing
            if (i + 1 < q.size() && q[i + 1] >= 0xDC00 && q[i + 1] <= 0xDFFF && (int)u.size() + 2 <= max_len) {
                u += c;
                u += q[++i];
                typed++;
            } else if (i + 1 < q.size() && q[i + 1] >= 0xDC00 && q[i + 1] <= 0xDFFF)
                i++;
            continue;
        }
        if ((int)u.size() < max_len) {
            u += c;
            typed++;
        }
    }
    return to_utf8(u);
}

} // namespace typed_text
