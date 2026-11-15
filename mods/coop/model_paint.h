// The three paint layers of a creature model record (bem): base, coat and detail skin effect ids, three dwords at
// byte 26 of the model's unpacked header (BUG-0369). The record is byte-run packed: 0xFE n = the byte before, n more
// times; FE 00 = the byte 0xFE. Portable (tests/validate_tests.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace model_paint {

constexpr size_t kEffects = 26; // the unpacked header: base, coat, detail effect ids from here

// before BUG-0852: false when a run touches the 12 effect bytes ("an id is not written byte by byte")
inline bool effects_strict(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3])
{
    size_t dec = 0, p = 0;
    size_t first[12] = {}, last[12] = {};
    uint8_t val[12] = {};
    while (p < m.size() && dec < kEffects + 12) {
        size_t start = p;
        uint8_t b = m[p];
        if (b == 0xFE) {
            if (p + 1 >= m.size())
                return false;
            if (m[p + 1] != 0) { // a run: n more of the byte before
                size_t n = m[p + 1];
                if (dec + n > kEffects && dec < kEffects + 12)
                    return false;
                dec += n;
                p += 2;
                continue;
            }
            p += 2;
        } else
            p++;
        if (dec >= kEffects) {
            first[dec - kEffects] = start;
            last[dec - kEffects] = p;
            val[dec - kEffects] = b;
        }
        dec++;
    }
    if (dec < kEffects + 12)
        return false;
    for (int k = 0; k < 3; k++) {
        eff[k] = val[4 * k] | val[4 * k + 1] << 8 | val[4 * k + 2] << 16 | (uint32_t)val[4 * k + 3] << 24;
        at[k] = first[4 * k];
        len[k] = last[4 * k + 3] - first[4 * k];
    }
    return true;
}

// BUG-0852: the ids with the runs decoded (a model whose coat and detail are 0 is written `00 FE 07`: the strict
// reader gave up on the whole model, so its base paint was never checked or substituted). patch[k]: layer k's four
// bytes are written byte by byte (at / len say where): only those can be rewritten in place.
inline bool effects_read(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3], bool patch[3])
{
    size_t dec = 0, p = 0;
    size_t first[12] = {}, last[12] = {};
    uint8_t val[12] = {};
    bool lit[12] = {};
    bool have_prev = false;
    uint8_t prev = 0;
    while (p < m.size() && dec < kEffects + 12) {
        size_t start = p;
        uint8_t b = m[p];
        if (b == 0xFE) {
            if (p + 1 >= m.size())
                return false;
            if (m[p + 1] != 0) { // a run: n more of the byte before
                if (!have_prev)
                    return false;
                size_t n = m[p + 1];
                for (size_t r = 0; r < n && dec < kEffects + 12; r++, dec++)
                    if (dec >= kEffects) {
                        first[dec - kEffects] = start;
                        last[dec - kEffects] = start + 2;
                        val[dec - kEffects] = prev;
                        lit[dec - kEffects] = false;
                    }
                p += 2;
                continue;
            }
            p += 2;
        } else
            p++;
        if (dec >= kEffects) {
            first[dec - kEffects] = start;
            last[dec - kEffects] = p;
            val[dec - kEffects] = b;
            lit[dec - kEffects] = true;
        }
        prev = b;
        have_prev = true;
        dec++;
    }
    if (dec < kEffects + 12)
        return false;
    for (int k = 0; k < 3; k++) {
        eff[k] = val[4 * k] | val[4 * k + 1] << 8 | val[4 * k + 2] << 16 | (uint32_t)val[4 * k + 3] << 24;
        patch[k] = lit[4 * k] && lit[4 * k + 1] && lit[4 * k + 2] && lit[4 * k + 3];
        at[k] = first[4 * k];
        len[k] = last[4 * k + 3] - first[4 * k];
    }
    return true;
}

// the reader the mod uses: with runs_fix the decoded one, else the strict one (every layer patchable)
inline bool effects(const std::vector<uint8_t>& m, uint32_t eff[3], size_t at[3], size_t len[3], bool patch[3],
                    bool runs_fix)
{
    if (runs_fix)
        return effects_read(m, eff, at, len, patch);
    patch[0] = patch[1] = patch[2] = true;
    return effects_strict(m, eff, at, len);
}

} // namespace model_paint
