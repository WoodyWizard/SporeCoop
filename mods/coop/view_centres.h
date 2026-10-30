// The host's "view centres": one point per player (the host first, then every partner) around which
// the host's game must keep its world alive and populated (BUG-0174). Spore is single-player: each
// stage streams, spawns and culls only around the local player's view. In co-op the host's game is
// the one world, so every player needs the area around it kept there, not only the host.
//
// Generic on purpose (any stage, any number of players up to kMaxCentres): the Cell stage feeds it
// from the partners' puppet cells (cellsync.cpp) and runs one population pass per centre; another
// stage (Creature, Rivals in 0.8: every player plays far from the host) can keep its own instance and
// ask `near_any` / `nearest` for culling and for which entities each guest gets.
// Portable (no Windows, no game): unit-tested in tests/cell_tests.cpp.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace view {

struct Centre {
    int id = 0; // player id (0 = the host)
    float x = 0, y = 0, z = 0;
};

// a 2D box (the Cell stage's view and populate boxes are 2D: z is 0 on the gameplay layer)
struct Box {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    // the same box moved so that its centre is at (x, y)
    Box centred(float x, float y) const
    {
        float dx = x - cx(), dy = y - cy();
        return {x0 + dx, y0 + dy, x1 + dx, y1 + dy};
    }
};

// at most this many centres (4 players today, room for 8): the cost of the passes is bounded by it
constexpr std::size_t kMaxCentres = 8;

class Centres {
public:
    void clear() { list_.clear(); }
    // the host's own centre: always the first entry
    void set_host(float x, float y, float z)
    {
        if (list_.empty() || list_[0].id != 0)
            list_.insert(list_.begin(), Centre{0, x, y, z});
        else
            list_[0] = Centre{0, x, y, z};
    }
    // a partner's centre (replaces an older one of the same id); false when the list is full
    bool add(int id, float x, float y, float z)
    {
        for (auto& c : list_)
            if (c.id == id) {
                c = Centre{id, x, y, z};
                return true;
            }
        if (list_.size() >= kMaxCentres)
            return false;
        list_.push_back(Centre{id, x, y, z});
        return true;
    }
    const std::vector<Centre>& list() const { return list_; }
    std::size_t size() const { return list_.size(); }
    const Centre* find(int id) const
    {
        for (auto& c : list_)
            if (c.id == id)
                return &c;
        return nullptr;
    }
    // the 2D distance to the nearest centre (a huge value without any)
    float nearest(float x, float y) const
    {
        float best = 3.0e38f;
        for (auto& c : list_)
            best = std::fmin(best, std::hypot(x - c.x, y - c.y));
        return best;
    }
    bool near_any(float x, float y, float r) const { return nearest(x, y) <= r; }

private:
    std::vector<Centre> list_;
};

} // namespace view
