// Whose is it, across a save and a load (E1 of docs/plan-0.7.15.md; BUG-1153 Civ cities and vehicles, BUG-1154 Tribe
// members): the owner a player had for a game object (a city, a vehicle, a tribe member) kept by its position and the
// owner's stable key (player_store.h), written into the per-player store on the host's disk with every save and given
// back to the same objects after the world's load. docs/01-architecture.md#arch-player-store.
//
// The game's objects are new after a load (other addresses), so the only identity is where they stand: the save keeps
// every position as it was written. match() pairs today's objects with the kept spots one to one, nearest pairs first,
// within a radius. A claim belongs to the player of its key whether or not that player is here: while it is away the
// host holds the object for it (never another guest), and its join gives it back.
//
// Portable (no game): tests/store_tests.cpp runs it.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "player_store.h"

namespace claims {

struct Spot {
    float x = 0, y = 0, z = 0;
    std::string key;   // the owner's stable key ("host", "s:<hex>"), or "shared" (Civ: the shared capital)
    uint8_t flags = 0; // the feature's (Civ: 1 gifted, 2 captured; Tribe: 1 born for the player, 2 a gift)
};

constexpr size_t kMaxSpots = 2000;

inline float dist2(float ax, float ay, float az, const Spot& s)
{
    float dx = ax - s.x, dy = ay - s.y, dz = az - s.z;
    return dx * dx + dy * dy + dz * dz;
}

// objs: the positions of today's objects. The result: per object the index of its spot, or -1. One spot to one object,
// the nearest pairs first, at most `radius` apart
inline std::vector<int> match(const std::vector<std::array<float, 3>>& objs, const std::vector<Spot>& spots, float radius)
{
    struct Pair {
        float d;
        int o, s;
    };
    std::vector<Pair> v;
    float r2 = radius * radius;
    for (size_t o = 0; o < objs.size(); o++)
        for (size_t s = 0; s < spots.size(); s++) {
            float d = dist2(objs[o][0], objs[o][1], objs[o][2], spots[s]);
            if (std::isfinite(d) && d <= r2)
                v.push_back({d, (int)o, (int)s});
        }
    std::sort(v.begin(), v.end(), [](const Pair& a, const Pair& b) { return a.d < b.d || (a.d == b.d && a.o < b.o); });
    std::vector<int> out(objs.size(), -1);
    std::vector<bool> used(spots.size(), false);
    for (auto& p : v)
        if (out[p.o] < 0 && !used[p.s]) {
            out[p.o] = p.s;
            used[p.s] = true;
        }
    return out;
}

inline bool key_ok(const std::string& k) { return k == "shared" || pstore::key_ok(k); }

// u16 n, n x {f32 x, y, z, str8 key, u8 flags}
inline void put_spots(pstore::Out& o, const std::vector<Spot>& v)
{
    uint16_t n = (uint16_t)std::min(v.size(), kMaxSpots);
    o.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        o.f32(v[i].x);
        o.f32(v[i].y);
        o.f32(v[i].z);
        o.str(v[i].key);
        o.u8(v[i].flags);
    }
}
inline bool get_spots(pstore::In& in, std::vector<Spot>& v)
{
    v.clear();
    uint16_t n = in.u16();
    if (!in.ok || n > kMaxSpots)
        return false;
    for (uint16_t i = 0; i < n && in.ok; i++) {
        Spot s;
        s.x = in.f32();
        s.y = in.f32();
        s.z = in.f32();
        s.key = in.str();
        s.flags = in.u8();
        if (!in.ok || !key_ok(s.key) || !std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z))
            return false;
        v.push_back(std::move(s));
    }
    return in.ok;
}

// the owner of a claim now: the present player of its key, else the host holds it for the absent one. `present`:
// the stable key -> session id of every player here (the host as "host")
template <class Present>
inline uint8_t holder(const std::string& key, Present present, uint8_t host_id = 0)
{
    int id = present(key);
    return id >= 0 ? (uint8_t)id : host_id;
}

} // namespace claims
