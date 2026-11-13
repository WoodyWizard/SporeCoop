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
// BUG-1041: who may send each kind, one table for every MSG_SPACE kind (as BUG-0701's message classes). A new kind is
// added here with its direction: tests/validate_tests.cpp fails for a kind of a space source file that is not listed.
//   Host:  only the host sends it (its tables, its decisions, what it kept for a rejoiner)
//   Guest: only a guest sends it (to the host: its own state, an ask, a done)
//   Any:   every player (its own events; gifts and trades addressed to a player)
enum class SpaceDir : uint8_t { Unknown, Any, Host, Guest };
inline SpaceDir space_dir(uint8_t kind)
{
    switch (kind) {
    // spacecoop.cpp: K_DOWN 1, K_RAID 2, K_MISSION 3, K_TERRA 4, K_GIFT 5, K_GIFT_BACK 6, K_SCULPT 7, K_COLOUR 8, K_TERRA_REC 9
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        return SpaceDir::Any;
    // space_map: K_ROSTER 10, K_ROSTER_REQ 11 (any); K_KEPT 12 (host); K_KEPT_DONE 13 (guest)
    case 10: case 11: return SpaceDir::Any;
    case 12: return SpaceDir::Host;
    case 13: return SpaceDir::Guest;
    // space_contracts: K_STATE 20, K_RESULT 22 (host); K_ACCEPT 21 (guest)
    case 20: case 22: return SpaceDir::Host;
    case 21: return SpaceDir::Guest;
    // space_diplomacy: K_EVENT 30, K_STATE 31, K_CONTACT 34 (any); K_SNAP 32 (host); K_ASK 33 (guest)
    case 30: case 31: case 34: return SpaceDir::Any;
    case 32: return SpaceDir::Host;
    case 33: return SpaceDir::Guest;
    // space_finale: K_STATE 40, K_WAIT 41, K_REACHED 42 (any)
    case 40: case 41: case 42: return SpaceDir::Any;
    // space_rivalry: K_STATE 50, K_EVENT 52, K_SET 57 (host); K_REQ 51, K_MERGE_FIX 55, K_ASK 56 (guest);
    // K_HIT 53, K_MONEY 54 (any)
    case 50: case 52: case 57: return SpaceDir::Host;
    case 51: case 55: case 56: return SpaceDir::Guest;
    case 53: case 54: return SpaceDir::Any;
    // space_allies: K_RAIDS 60, K_DEFENDED 61 (any)
    case 60: case 61: return SpaceDir::Any;
    // space_trade: K_TOOL .. K_OFFER_CANCEL 70..76 (any, addressed); K_INV 77, K_INV_DONE 79 (guest); K_INV_KEPT 78 (host)
    case 70: case 71: case 72: case 73: case 74: case 75: case 76: return SpaceDir::Any;
    case 77: case 79: return SpaceDir::Guest;
    case 78: return SpaceDir::Host;
    // space_planets: K_SPICE 80 (host); K_SPICE_TAKEN 81 (guest)
    case 80: return SpaceDir::Host;
    case 81: return SpaceDir::Guest;
    // space_badges: K_BADGES 85, K_BADGES_DONE 87 (guest); K_BADGES_KEPT 86 (host)
    case 85: case 87: return SpaceDir::Guest;
    case 86: return SpaceDir::Host;
    default:
        return SpaceDir::Unknown; // no feature reads it: passed on as before (a newer peer's kind)
    }
}
// the list before BUG-1041 (`coop.fix space.dir_table off`): contracts K_STATE 20 / K_RESULT 22, diplomacy K_SNAP 32,
// rivalry K_STATE 50 / K_EVENT 52 / K_SET 57
inline bool space_host_only(uint8_t kind, bool table = true)
{
    if (table)
        return space_dir(kind) == SpaceDir::Host;
    return kind == 20 || kind == 22 || kind == 32 || kind == 50 || kind == 52 || kind == 57;
}
// May a message with this header be used (and, on the host, passed on)? The host gets the real
// sender in `from`: a guest speaks only for itself and never sends a host-only kind. A guest gets
// everything from the host (from 0): host-only kinds must be the host's own (origin 0), guest-only
// kinds (BUG-1041) a guest's, never the host's. `table`: the caller's switch (space.dir_table), not a lab switch here
inline bool space_origin_ok(bool is_host, uint8_t from, uint8_t origin, uint8_t kind, bool table = true) // not a lab switch: a parameter
{
    if (is_host)
        return from != 0 && origin == from && !space_host_only(kind, table);
    if (from != 0)
        return false;
    if (space_host_only(kind, table))
        return origin == 0;
    return !(table && space_dir(kind) == SpaceDir::Guest && origin == 0);
}

// ---- received world effects (effects.cpp, pool 5) ----
// BUG-1048: the 16 floats after the effect id: position, uniform scale, 3x3 matrix. Finite, the position within
// `pos_max`, and (`scale`) the scale in the sender's range 0 < s <= 1000 (effects.cpp copy_transform); the game used to
// get a scale of 0, a negative one or 1e30 from a broken or hostile peer
constexpr float kEffectScaleMax = 1000.f;
inline bool effect_xf_ok(const float* f, float pos_max, bool scale = true)
{
    for (int i = 0; i < 16; i++)
        if (!std::isfinite(f[i]))
            return false;
    if (!(std::fabs(f[0]) < pos_max && std::fabs(f[1]) < pos_max && std::fabs(f[2]) < pos_max))
        return false;
    return !scale || (f[3] > 0.f && f[3] <= kEffectScaleMax);
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
