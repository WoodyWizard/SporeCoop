// Decision 6F A (owner 2026-11-18, "6FC": A first, then the galaxy server's fan-out): the compact Cell state.
// The classic MSG_CELL_STATE sends every cell as a full 63-byte record 10 times a second; about half of it never changes
// while the cell lives. MSG_CELL_STATE_C (to a guest with net::CAP_COMPACT_CELL) sends:
//   chunk header: u16 cycle, u8 chunk, u8 chunks, u8 n, f32 the host's world scale, f32 x 3 the anchor (the guest's
//   centre), f32 the position step, f32 the velocity step
//   n records: u32 the host index with kKeep / kFull in bits 15 / 14 (a pool slot is below 4096: those bits are free)
//     keep: nothing more (the cell is still there, the guest goes on along its velocity)
//     else: i16 x 3 position - anchor in steps, i16 x 2 velocity in steps, i8 x 4 the rotation (x 127), u16 the size
//       (log2, kSizeBias), u8 hp, u8 flags; full: then u32 resource instance, u32 x 3 model key, u8 level
// The host sends a cell's full record when the guest has not had it for kFullEvery seconds (lost packets heal) or its
// key or level changed; a cell outside the guest's near box is sent every kFarEvery cycles, a keep the others.
// Pure helpers here (tests/cell_tests.cpp).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cell_compact {

constexpr uint32_t kKeep = 0x8000, kFull = 0x4000, kFlagBits = kKeep | kFull;
constexpr double kFullEvery = 2.0;
constexpr uint32_t kFarEvery = 5;          // a far cell: 2 times a second at the host's 10
constexpr float kNearX = 40.f, kNearY = 32.f; // the near box about the guest's centre (its view is about 15 x 11)
constexpr size_t kChunkBytes = 950;        // a chunk's payload (the classic: 14 x 63 + 9)
constexpr size_t kMaxRecord = 4 + 18 + 17;
constexpr int kSizeBias = 32768, kSizeScale = 2048; // size = 2^((u - bias) / scale); 0: zero

inline bool encodable(uint32_t idx) { return (idx & kFlagBits) == 0; }
inline int16_t q16(float d, float step)
{
    if (!(step > 0.f) || !std::isfinite(d))
        return 0;
    const float v = std::round(d / step);
    return (int16_t)std::clamp(v, -32767.f, 32767.f);
}
inline float dq16(int16_t v, float step) { return (float)v * step; }
// the step that fits `span` (the largest offset) in 16 bits, never 0
inline float step_for(float span) { return std::max(std::isfinite(span) ? span : 1.f, 1e-3f) / 32000.f; }
inline int8_t q8(float c) { return (int8_t)std::clamp(std::round((std::isfinite(c) ? c : 0.f) * 127.f), -127.f, 127.f); }
inline uint16_t size_code(float size)
{
    if (!(size > 0.f) || !std::isfinite(size))
        return 0;
    const float u = std::round(std::log2(size) * kSizeScale) + kSizeBias;
    return (uint16_t)std::clamp(u, 1.f, 65535.f);
}
inline float size_of(uint16_t u) { return u ? std::exp2(((float)u - kSizeBias) / kSizeScale) : 0.f; }
// a far cell is sent in the cycles where (cycle + slot) % kFarEvery == 0 (spread over the cycles)
inline bool far_turn(uint32_t cycle, uint32_t idx) { return (cycle + (idx & 0xFFF)) % kFarEvery == 0; }
inline bool near_box(float dx, float dy) { return std::fabs(dx) <= kNearX && std::fabs(dy) <= kNearY; }

} // namespace cell_compact
