// Own DNA per player (WP-D of 0.7.16, BUG-1291; owner decision 2026-11-19; docs/03-gameplay.md#gameplay-own-dna).
//
// Model "party income, own wallet": every gain the party makes (anybody's kill, friend, goal card, nest egg) raises
// every player's DNA by the same amount, as the one shared pool did; what a player spends (its editor) comes off its
// own DNA only. The evolution bar (cPlayer +0x10F0, the stage's total earned: brain levels, migration) stays the
// party's: the host's.
//
// * The host keeps every guest's wallet (a Book): its Creature budget and its Cell spent count (the Cell DNA is
//   DNAForFood(food) - spent, the food is the party's), per world. The host's own DNA is its game's (its save holds it);
//   a guest's wallet lives only in the host's memory and in the per-player store beside the save (record "dna.own"),
//   so it survives a reload of the host's world, a host restart and a guest's rejoin. The host's game save never holds
//   a guest's DNA: nothing of a guest's wallet is written into the host's game.
// * Income: one observer instead of every reward path: each frame in Creature the host's bar step g > 0 is added to
//   every wallet of the world (present or not: the income is the party's). The bar only rises on gains (the game's
//   AddEvolutionPoints, also the mod's own payments of a guest's gain), never on spends (SetEvolutionPoints, BUG-0630),
//   so every cause is caught once, after the party's pace (dna_pace.h). A negative step (a load, a death setback) is
//   not passed on; a new store (the host's world opened again) or another world starts a new baseline.
// * First sight of a guest in a world without a record: its budget = the host's DNA now (a 0.7.15 save: every guest
//   starts with the number it saw in the shared pool); its Cell spent count = the host's count now.
// * Wire: MSG_DNA (sent per guest) carries the guest's own budget as its total and a tail after the pace byte (Tail
//   below: flags, the party bar, the world); the guest sets its DNA with SetEvolutionPoints and moves its bar to the
//   party's (never down). Cell: the wsync row cell_spent_own (key the player, v0 its spent count).
// * Who: the session setting all.shared_dna (0 own, the default; 1 the old shared pool; Rivals reads it as own) with
//   creature.mode own; a guest whose mod is older (creature / npc family minor below kMinor) plays the shared pool
//   with the host, as in 0.7.15 (its own DNA is the host's, its spends the host's).
//
// Portable (no game, no Windows): tests/dna_tests.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "net/buffer.h"

namespace dna_own {

// the family minors (creature for MSG_DNA's tail, npc for the wsync row) from which a peer reads its own wallet
constexpr uint16_t kMinor = 2;

// the own wallets are in play: the fix switch on, the session setting not "shared", every player its own creature
inline bool active(bool fix, bool shared_setting, bool host_creature_mode)
{
    return fix && !shared_setting && !host_creature_mode;
}

// a peer's mod reads its own wallet: it sent its families and the family's minor is kMinor or newer (same rev)
inline bool peer_reads(bool has_families, bool found, uint16_t rev, uint16_t minor, uint16_t my_rev)
{
    return has_families && found && rev == my_rev && minor >= kMinor;
}

struct Purse {
    uint32_t world = 0; // the world it belongs to (0: none)
    float value = 0;    // Creature: the budget; Cell: the spent count
};
struct Wallet {
    Purse creature, cell;
};

constexpr float kMaxStep = 100000.f; // a bar step bigger than this is no gain (a broken read)
constexpr float kMaxValue = 1.0e7f;

class Book {
public:
    std::map<std::string, Wallet> w; // stable key -> wallet
    // the bar observer
    float bar_last = NAN;
    uint32_t bar_world = 0, bar_serial = 0;
    uint32_t steps = 0;  // tests: gains credited
    float credited = 0;  // tests: their sum

    void clear()
    {
        w.clear();
        restart();
    }
    void restart() { bar_last = NAN; }

    // the host's bar now (NAN: none), its world (0: none), the store serial: the party's gain credited to every wallet
    // of the world (0 when none)
    float observe(float bar, uint32_t world, uint32_t serial)
    {
        if (!std::isfinite(bar) || !world) {
            bar_last = NAN;
            return 0;
        }
        if (!std::isfinite(bar_last) || world != bar_world || serial != bar_serial) {
            bar_last = bar;
            bar_world = world;
            bar_serial = serial;
            return 0;
        }
        float g = bar - bar_last;
        bar_last = bar;
        if (!(g > 0.001f) || g > kMaxStep)
            return 0; // a spend never moves it; a load or a setback lowers it: a new baseline
        for (auto& [k, x] : w)
            if (x.creature.world == world)
                x.creature.value = std::min(kMaxValue, x.creature.value + g);
        steps++;
        credited += g;
        return g;
    }

    // the Creature budget of `key` in `world`; first sight (no purse of this world): the host's DNA now
    float budget(const std::string& key, uint32_t world, float host_now, bool* made = nullptr)
    {
        Purse& p = w[key].creature;
        bool m = p.world != world;
        if (m) {
            p.world = world;
            p.value = std::max(0.f, host_now);
        }
        if (made)
            *made = m;
        return p.value;
    }
    bool has_budget(const std::string& key, uint32_t world) const
    {
        auto it = w.find(key);
        return it != w.end() && it->second.creature.world == world;
    }
    // a guest's own spend (s < 0) or a correction: never below 0; the new budget
    float add(const std::string& key, uint32_t world, float d, float host_now)
    {
        budget(key, world, host_now);
        Purse& p = w[key].creature;
        p.value = std::clamp(p.value + d, 0.f, kMaxValue);
        return p.value;
    }

    // the Cell spent count of `key` in `world`; first sight: the host's count now
    int spent(const std::string& key, uint32_t world, int host_spent, bool* made = nullptr)
    {
        Purse& p = w[key].cell;
        bool m = p.world != world;
        if (m) {
            p.world = world;
            p.value = (float)std::max(0, host_spent);
        }
        if (made)
            *made = m;
        return (int)p.value;
    }
    int spend_cell(const std::string& key, uint32_t world, int d, int host_spent)
    {
        spent(key, world, host_spent);
        Purse& p = w[key].cell;
        p.value = std::clamp(p.value + (float)d, 0.f, kMaxValue);
        return (int)p.value;
    }

    // the host's word (coop.store adopt): `from`'s wallet is `to`'s now
    void adopt(const std::string& from, const std::string& to)
    {
        auto it = w.find(from);
        if (it == w.end() || from == to)
            return;
        w[to] = it->second;
        w.erase(from);
    }
};

// ---- the store record "dna.own" v1: u32 creature world, f32 budget, u32 cell world, i32 spent (little endian)
constexpr const char* kStoreFeature = "dna.own";
constexpr uint16_t kStoreVersion = 1;

inline std::vector<uint8_t> encode(const Wallet& x)
{
    net::Writer o;
    o.u32(x.creature.world);
    o.f32(x.creature.value);
    o.u32(x.cell.world);
    o.u32((uint32_t)(int32_t)x.cell.value);
    return o.data;
}
inline bool decode(const std::vector<uint8_t>& d, Wallet& out)
{
    if (d.size() != 16)
        return false;
    net::Reader r(d.data(), d.size());
    Wallet x;
    x.creature.world = r.u32();
    x.creature.value = r.f32();
    x.cell.world = r.u32();
    x.cell.value = (float)(int32_t)r.u32();
    if (!r.ok() || !std::isfinite(x.creature.value) || x.creature.value < 0 || x.creature.value > kMaxValue ||
        x.cell.value < 0 || x.cell.value > kMaxValue)
        return false;
    out = x;
    return true;
}

// ---- MSG_DNA's tail (after the pace byte; a 0.7.15 guest stops before it): u8 flags, f32 bar, u32 world
constexpr uint8_t kFlagOwn = 1; // the total is this guest's own wallet
struct Tail {
    bool has = false;
    uint8_t flags = 0;
    float bar = NAN;   // the party bar (the host's cPlayer +0x10F0)
    uint32_t world = 0;
    void write(net::Writer& w) const
    {
        w.u8(flags);
        w.f32(bar);
        w.u32(world);
    }
    static Tail read(net::Reader& r)
    {
        Tail t;
        if (!r.ok() || r.left() < 9)
            return t;
        t.flags = r.u8();
        t.bar = r.f32();
        t.world = r.u32();
        t.has = r.ok();
        return t;
    }
    bool own() const { return has && (flags & kFlagOwn); }
};

// guest: what to do with an own total. set: SetEvolutionPoints(total); raise: AddEvolutionPoints(raise) first (the bar
// up to the party's; it adds the same to the DNA, which the set then replaces). The bar goes down only for a setback
// of the party's bar under one point (`setback`, BUG-1393): a creature's respawn drops the dying player's bar to its
// whole number (36.9 -> 36.0, lab 20261121-003202 mem_census); the host's death drops the party's so, and the guest's
// bar stayed 0.9 ahead until the next gain. A bigger fall (a host's load) is not followed (the bar never goes down there).
constexpr float kSetbackMax = 1.0f;
struct Apply {
    float raise = 0; // < 0: the party's setback
    bool set = false;
};
inline Apply apply_own(float total, float bar, float my_dna, float my_bar, bool setback = false)
{
    Apply a;
    if (std::isfinite(bar) && std::isfinite(my_bar) && bar - my_bar > 0.5f && bar - my_bar < kMaxStep)
        a.raise = bar - my_bar;
    else if (setback && std::isfinite(bar) && std::isfinite(my_bar) && my_bar - bar > 0.5f && my_bar - bar < kSetbackMax)
        a.raise = bar - my_bar;
    a.set = a.raise != 0 || std::fabs(total - my_dna) > 0.01f;
    return a;
}

} // namespace dna_own
