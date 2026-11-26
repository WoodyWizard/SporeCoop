// Pure UTF-8 word/CJK line wrapper and dialog font width estimator for multi-line tooltips (BUG-1584).
// Zero Win32/Spore dependencies so host unit tests (tests/text_tests.cpp) exercise the exact mod logic.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace tooltip_wrap {

// Pixel advance of each glyph in Spore's dialog body font at 1440x1080 (measured: 12 against 4 copies of each glyph
// in a label, docs/08-ui.md#ui-text-width): ASCII 32..126, Cyrillic U+0410..U+044F, CJK >= U+2E80.
inline float codepoint_width(uint32_t cp)
{
    static const unsigned char kAscii[95] = {
        6, 7, 7, 11, 11, 13, 13, 4, 7, 7, 8, 11, 7, 7, 7, 7, 11, 11, 11,
        11, 11, 11, 11, 11, 11, 11, 7, 7, 11, 11, 11, 8, 16, 12, 11, 12, 12, 11,
        11, 13, 13, 5, 10, 12, 11, 14, 13, 13, 11, 13, 12, 10, 12, 13, 12, 17, 11,
        13, 11, 7, 7, 7, 11, 11, 7, 10, 11, 10, 11, 11, 7, 10, 11, 6, 7, 10,
        6, 16, 11, 11, 11, 11, 8, 8, 8, 11, 11, 16, 11, 10, 10, 7, 5, 7, 11};
    static const unsigned char kCyr[64] = {
        12, 11, 11, 10, 14, 11, 18, 11, 13, 14, 12, 13, 15, 13, 13, 13, 11, 12, 12, 12, 15, 12, 13, 12,
        18, 18, 15, 16, 12, 12, 18, 12, 10, 11, 10, 9, 12, 11, 15, 9, 11, 11, 10, 11, 14, 11, 11, 11,
        11, 10, 9, 10, 15, 11, 11, 11, 15, 16, 12, 14, 10, 10, 15, 11};
    if (cp >= 32 && cp < 127)
        return (float)kAscii[cp - 32];
    if (cp >= 0x410 && cp < 0x450)
        return (float)kCyr[cp - 0x410];
    if (cp == 0x401 || cp == 0x451) // Yo / yo
        return 11.f;
    if (cp >= 0x2E80) // CJK ideographs / full-width punctuation
        return 16.f;
    return 11.f;
}

inline size_t next_cp(const std::string& t, size_t i, uint32_t& cp)
{
    unsigned char ch = (unsigned char)t[i];
    size_t n = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : (ch >> 3) == 30 ? 4 : 1;
    if (i + n > t.size())
        n = 1;
    if (n == 1) {
        cp = ch;
    } else {
        cp = ch & (0x3Fu >> (n - 1));
        for (size_t k = 1; k < n; k++) {
            unsigned char b = (unsigned char)t[i + k];
            if ((b & 0xC0) != 0x80) {
                cp = ch;
                return 1;
            }
            cp = (cp << 6) | (b & 0x3Fu);
        }
    }
    return n;
}

inline float measure(const std::string& t)
{
    float w = 0.f;
    for (size_t i = 0; i < t.size();) {
        uint32_t cp = 0;
        i += next_cp(t, i, cp);
        w += codepoint_width(cp);
    }
    return w;
}

inline bool is_cjk_breakable(uint32_t cp)
{
    // Break freely between CJK ideographs and after CJK punctuation (except opening brackets)
    if (cp >= 0x2E80)
        return true;
    return false;
}

inline std::string trim_right_spaces(std::string s)
{
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

inline std::string fit_ellipsis(std::string s, float max_px)
{
    s = trim_right_spaces(std::move(s));
    const std::string ell = "...";
    float ell_w = measure(ell);
    while (!s.empty() && measure(s) + ell_w > max_px) {
        size_t n = s.size() - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
            n--;
        s.resize(n);
        s = trim_right_spaces(std::move(s));
    }
    return s + ell;
}

// Wrap `text` into at most `max_lines` lines, each measuring <= `max_px` (with best-effort single-glyph floor).
// Explicit '\n' forces a new line. Sets `*truncated = true` if content exceeded `max_lines`.
inline std::vector<std::string> wrap(const std::string& text, float max_px, int max_lines, bool* truncated = nullptr)
{
    if (truncated)
        *truncated = false;
    std::vector<std::string> out;
    if (text.empty() || max_lines <= 0)
        return out;
    if (max_px < 32.f)
        max_px = 32.f;

    auto push_line = [&](std::string line, bool more_follows) -> bool {
        line = trim_right_spaces(std::move(line));
        if ((int)out.size() + 1 >= max_lines && more_follows) {
            out.push_back(fit_ellipsis(std::move(line), max_px));
            if (truncated)
                *truncated = true;
            return false; // stop
        }
        out.push_back(std::move(line));
        return (int)out.size() < max_lines;
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        bool has_more_paras = (nl != std::string::npos);
        std::string para = has_more_paras ? text.substr(pos, nl - pos) : text.substr(pos);
        pos = has_more_paras ? nl + 1 : text.size() + 1;

        // Strip trailing CR if any
        if (!para.empty() && para.back() == '\r')
            para.pop_back();

        if (para.empty()) {
            if (!push_line("", has_more_paras))
                return out;
            continue;
        }

        // Split paragraph into wrapped lines by scanning UTF-8 codepoints and remembering safe break points.
        size_t line_start = 0;
        while (line_start < para.size()) {
            // Skip leading spaces on wrapped continuation lines
            while (line_start < para.size() && para[line_start] == ' ')
                line_start++;
            if (line_start >= para.size())
                break;

            float w = 0.f;
            size_t i = line_start;
            size_t last_space_break = std::string::npos; // index right before space
            size_t last_cjk_break = std::string::npos;   // index right after a CJK glyph
            size_t last_fit_end = line_start;

            while (i < para.size()) {
                uint32_t cp = 0;
                size_t step = next_cp(para, i, cp);
                float cw = codepoint_width(cp);
                if (i > line_start && w + cw > max_px)
                    break;
                w += cw;
                if (cp == ' ')
                    last_space_break = i;
                i += step;
                last_fit_end = i;
                if (is_cjk_breakable(cp))
                    last_cjk_break = i;
            }

            if (i >= para.size()) {
                // Entire remaining paragraph fits on this line
                if (!push_line(para.substr(line_start), has_more_paras))
                    return out;
                break;
            }

            // Need to wrap before `i`: prefer space break, else CJK break, else hard UTF-8 codepoint break.
            size_t cut = last_fit_end;
            size_t next_start = last_fit_end;
            if (last_space_break != std::string::npos && last_space_break > line_start) {
                cut = last_space_break;
                next_start = last_space_break + 1;
            } else if (last_cjk_break != std::string::npos && last_cjk_break > line_start) {
                cut = last_cjk_break;
                next_start = last_cjk_break;
            } else if (cut == line_start) {
                // Always advance at least one code point
                uint32_t cp = 0;
                cut = line_start + next_cp(para, line_start, cp);
                next_start = cut;
            }

            while (next_start < para.size() && para[next_start] == ' ')
                next_start++;
            bool more = (next_start < para.size()) || has_more_paras;
            if (!push_line(para.substr(line_start, cut - line_start), more))
                return out;
            line_start = next_start;
        }
    }
    return out;
}

// Flatten newlines to " | " for single-line diagnostics (coop.ui state).
inline std::string single_line(const std::string& text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\r')
            continue;
        if (c == '\n')
            out += " | ";
        else
            out.push_back(c);
    }
    return out;
}

} // namespace tooltip_wrap
