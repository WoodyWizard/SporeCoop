// Checks for values that come from another player (portable; tests/validate_tests.cpp).
// A joined player's game can send anything: every handler reads its message completely before it
// changes anything, and the checks that are the same everywhere live here.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {

// ---- a buffer sent in pieces of `chunk` bytes (MSG_CREATION) ----
struct ChunkRx {
    bool started = false;
    uint32_t id = 0, total = 0, have = 0;
    std::vector<uint8_t> data;
    std::vector<bool> got;
};
enum class Chunk { Drop, Partial, Complete };

// One piece `p` (n bytes) at `off` of the buffer `id` of `total` bytes. Every piece of a buffer
// carries the same total; pieces start at multiples of `chunk` and are exactly
// min(chunk, total - off) long; a repeated piece is ignored. A new id starts a new buffer.
inline Chunk chunk_add(ChunkRx& rx, uint32_t id, uint32_t total, uint32_t off, const uint8_t* p, size_t n,
                       size_t chunk, uint32_t max_total)
{
    if (!chunk || total > max_total || off % chunk || off > total)
        return Chunk::Drop;
    size_t want = total - off < chunk ? total - off : chunk;
    if (n != want || (total && off == total))
        return Chunk::Drop;
    if (!rx.started || rx.id != id) {
        rx = ChunkRx{};
        rx.started = true;
        rx.id = id;
        rx.total = total;
        rx.data.assign(total, 0);
        rx.got.assign((total + chunk - 1) / chunk + 1, false);
    } else if (rx.total != total)
        return Chunk::Drop; // the same buffer with another size: not from our sender
    size_t ci = off / chunk;
    if (ci >= rx.got.size() || (uint64_t)off + n > rx.data.size())
        return Chunk::Drop;
    if (!rx.got[ci]) {
        if (n)
            memcpy(rx.data.data() + off, p, n);
        rx.got[ci] = true;
        rx.have += (uint32_t)n;
    }
    return rx.have >= rx.total ? Chunk::Complete : Chunk::Partial;
}

// ---- snapshots sent in chunks of one cycle (tribe, civ guest_state) ----
// a chunk number that can belong to the cycle: chunks > 0 and chunk < chunks
inline bool cycle_chunk_ok(uint8_t chunk, uint8_t chunks) { return chunks > 0 && chunk < chunks; }

// BUG-0612 (part: older cycles, net.cycle_order): the cycle a chunk belongs to against the cycle being received.
// A chunk of an OLDER cycle that comes late (a loss burst, reordering) used to start that cycle again: the newer
// cycle's chunks were forgotten (it never completed, so nothing was cleaned up) and the older states overwrote the
// newer ones. Cycles are u16 counters that wrap: "older" is within half the range behind.
enum class Cycle { Same, Newer, Older };
inline Cycle cycle_order(uint16_t cycle, uint16_t current, bool have_current)
{
    if (!have_current)
        return Cycle::Newer;
    if (cycle == current)
        return Cycle::Same;
    return (uint16_t)(cycle - current) < 0x8000 ? Cycle::Newer : Cycle::Older;
}

// one tracker for the chunked snapshots of Tribe and Civ: which chunks of the current cycle came
struct CycleTracker {
    uint16_t cur = 0;
    bool have = false;
    uint64_t got[4] = {0, 0, 0, 0}; // chunk bits 0..255
    uint32_t older = 0;             // chunks of an older cycle dropped
    // a chunk of `cycle` came: false = drop it (an older cycle); a newer cycle starts empty (`started` true)
    bool begin(uint16_t cycle, bool keep_order, bool* started = nullptr)
    {
        Cycle o = cycle_order(cycle, cur, have);
        if (o == Cycle::Older && keep_order) {
            older++;
            return false;
        }
        if (started)
            *started = o != Cycle::Same;
        if (o != Cycle::Same) {
            cur = cycle;
            have = true;
            got[0] = got[1] = got[2] = got[3] = 0;
        }
        return true;
    }
    void add(uint8_t chunk) { got[chunk >> 6] |= 1ull << (chunk & 63); }
    unsigned count() const
    {
        unsigned n = 0;
        for (uint64_t g : got)
            for (uint64_t x = g; x; x &= x - 1)
                n++;
        return n;
    }
    bool complete(uint8_t chunks) const { return count() >= chunks; }
    void reset() { *this = CycleTracker{}; }
};

// ---- MSG_SPACE: u8 kind, u8 origin, payload (spacecoop.cpp, space_ext.h) ----
// kinds only the host sends: contracts K_STATE 20 / K_RESULT 22, diplomacy K_SNAP 32, rivalry
// K_STATE 50 / K_EVENT 52 / K_SET 57
inline bool space_host_only(uint8_t kind)
{
    return kind == 20 || kind == 22 || kind == 32 || kind == 50 || kind == 52 || kind == 57;
}
// May a message with this header be used (and, on the host, passed on)? The host gets the real
// sender in `from`: a guest speaks only for itself and never sends a host-only kind. A guest gets
// everything from the host (from 0): host-only kinds must be the host's own (origin 0).
inline bool space_origin_ok(bool is_host, uint8_t from, uint8_t origin, uint8_t kind)
{
    if (is_host)
        return from != 0 && origin == from && !space_host_only(kind);
    return from == 0 && (!space_host_only(kind) || origin == 0);
}

// ---- players ----
// `id` is a player of the session now (Session::players(): including this machine)
template <class S> bool is_player(const S& sess, uint8_t id)
{
    for (auto& p : sess.players())
        if (p.id == id)
            return true;
    return false;
}

// ---- numbers ----
// damage a player reports: finite, not negative, at most `cap`; anything else is 0
inline float sane_damage(float d, float cap)
{
    if (!std::isfinite(d) || d <= 0)
        return 0;
    return d > cap ? cap : d;
}
inline bool finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

// n units at `price` each as a sum of money, false when it does not fit an int
inline bool money_total(int n, int price, int& out)
{
    long long t = (long long)n * (long long)price;
    if (n < 0 || price < 0 || t > 0x7FFFFFFFll)
        return false;
    out = (int)t;
    return true;
}

} // namespace net
