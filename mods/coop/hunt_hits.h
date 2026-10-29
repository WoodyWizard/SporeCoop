// The joint-hunt hit table (teamplay.cpp), portable for the unit tests (tests/npc_tests.cpp).
#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace teamplay {

// Recent player hits per target creature. Every hit prunes the hits older than the window of every
// target, so the table holds only targets hit within the window (BUG-0099: only the hit target was
// pruned and the table kept one entry per creature ever hit; past 256 entries every hit walked the
// whole table and removed nothing). `forget` drops a target whose object the game freed (its
// address may come back as another creature). `fix` false keeps the old pruning (tests, coop.fix
// team.hunt_hits off).
class HitTable {
public:
    explicit HitTable(double window) : m_window(window) {}

    // a hit by `player` on `target` at `now`: true when another player hit it within the window
    bool hit(const void* target, uint8_t player, double now)
    {
        if (fix)
            prune(now);
        auto& v = m_hits[target];
        bool joint = false;
        for (size_t i = 0; i < v.size();) {
            if (now - v[i].t > m_window) {
                v.erase(v.begin() + i);
                continue;
            }
            joint |= v[i].player != player;
            i++;
        }
        v.push_back({player, now});
        if (!fix && m_hits.size() > 256) // the code before BUG-0099
            for (auto it = m_hits.begin(); it != m_hits.end();)
                it = it->second.empty() ? m_hits.erase(it) : std::next(it);
        return joint;
    }
    void prune(double now)
    {
        for (auto it = m_hits.begin(); it != m_hits.end();) {
            auto& v = it->second;
            size_t keep = 0;
            for (auto& h : v)
                if (now - h.t <= m_window)
                    v[keep++] = h;
            v.resize(keep);
            it = v.empty() ? m_hits.erase(it) : std::next(it);
        }
    }
    void forget(const void* target) { m_hits.erase(target); }
    void clear() { m_hits.clear(); }
    size_t size() const { return m_hits.size(); }

    bool fix = true; // BUG-0099; false: the pruning before the fix

private:
    struct Hit {
        uint8_t player;
        double t;
    };
    double m_window;
    std::map<const void*, std::vector<Hit>> m_hits;
};

} // namespace teamplay
