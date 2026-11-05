// Portable rules of the whole-planet re-alignment (realign.cpp, BUG-0564 / BUG-0567 class W2 of 0.7.7);
// tests/npc_tests.cpp runs them.
//
// The host's game owns the planet: after a transition (its game over and the reload of its last save, a
// guest's restart and rejoin, a load, an editor) a guest's game must hold the host's villages, huts,
// tools, nests and cities on the whole planet, not only the 250 m around the guest. The host sends its
// whole list (realign.cpp); a guest matches it with its own list per kind by position (nearest partner
// within the kind's tolerance, the player's own village only with the host's player village), then
// * an object of its own without a host partner on `kExtraLooks` complete lists in a row is extra: it
//   goes through the game's own removal (the village with its hut, tools, food mat and map mark);
// * a host object without a partner is missing (counted; the stage's own path makes it: tribe.npc_tribes);
// * a bound pair whose state differs (a hut's health) gets the host's state.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace realign {

struct Obj {
    float x = 0, y = 0, z = 0;
    uint16_t st = 0;  // the kind's state (huts / tools: health in 1/1000 of the maximum)
    uint8_t fl = 0;   // bit 0: the player's own (village, hut)
    uint32_t key = 0; // this game's key of the object (index into the caller's list)
};
constexpr uint8_t kFlPlayer = 1;
constexpr int kExtraLooks = 2; // complete host lists in a row without a partner before an object goes

inline float dist(const Obj& a, const Obj& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct Match {
    std::vector<std::pair<int, int>> bound; // (host index, local index)
    std::vector<int> lack;                  // host indices without a local partner
    std::vector<int> extra;                 // local indices without a host partner
};

// greedy by distance: the closest pairs first, each object bound at most once, the player flag equal
inline Match match(const std::vector<Obj>& host, const std::vector<Obj>& local, float tol)
{
    struct P {
        float d;
        int h, l;
    };
    std::vector<P> ps;
    for (int h = 0; h < (int)host.size(); h++)
        for (int l = 0; l < (int)local.size(); l++) {
            if ((host[h].fl & kFlPlayer) != (local[l].fl & kFlPlayer))
                continue;
            float d = dist(host[h], local[l]);
            if (d <= tol)
                ps.push_back({d, h, l});
        }
    std::sort(ps.begin(), ps.end(), [](const P& a, const P& b) { return a.d < b.d; });
    std::vector<char> hu(host.size()), lu(local.size());
    Match m;
    for (auto& p : ps)
        if (!hu[p.h] && !lu[p.l]) {
            hu[p.h] = lu[p.l] = 1;
            m.bound.push_back({p.h, p.l});
        }
    for (int h = 0; h < (int)host.size(); h++)
        if (!hu[h])
            m.lack.push_back(h);
    for (int l = 0; l < (int)local.size(); l++)
        if (!lu[l])
            m.extra.push_back(l);
    return m;
}

// Extra objects seen on complete lists in a row (keyed by the caller's stable key: pointer bits).
// `due` gets the keys that were extra on kExtraLooks lists in a row; an object with a partner again,
// or gone, starts over. A host list without any object of the kind while this game has more than two
// is no list of the planet (the host's world is not there yet: a load, a stage change): nothing is due.
struct Streaks {
    std::map<uint64_t, int> n;
    void step(const std::vector<uint64_t>& extra_keys, size_t host_count, size_t local_count, std::vector<uint64_t>& due)
    {
        due.clear();
        if (host_count == 0 && local_count > 2) {
            n.clear();
            return;
        }
        std::map<uint64_t, int> next;
        for (uint64_t k : extra_keys) {
            auto it = n.find(k);
            int c = (it == n.end() ? 0 : it->second) + 1;
            next[k] = c;
            if (c >= kExtraLooks)
                due.push_back(k);
        }
        n.swap(next);
    }
    void clear() { n.clear(); }
};

// a state worth fixing: huts / tools health (1/1000) more than 2 % apart
inline bool state_off(uint16_t host, uint16_t local) { return (host > local ? host - local : local - host) > 20; }

} // namespace realign
