// The view centres of all players (BUG-0280, docs/re/creature.md#creature-view-centres).
//
// The game simulates its world only around one point: the local camera. In the Creature stage the
// relevance pass (SporeCoop::Relevance_Update, every 5 s) enables the herds and spawn points within
// 100 m of that point and sends creatures farther than 250 m back to the creature pool; a guest far
// from the host had no NPCs around it. The host keeps a list of the partners' view centres (their
// last positions in the host's own place) and the stage hooks add them to the game's own list, so
// the world is simulated around every player, at the game's own radii (a bounded cost: one more
// sphere per partner, 2-4 players).
//
// The registry is stage-neutral: coop.cpp notes a partner's position whenever its puppet is shown
// in the host's place (any stage; in Tribe the partner's pointer), the stage code asks for the
// centres of its mode. The hook of the Creature and Tribe stages lives in view_centres.cpp; the Cell
// stage (cellsync.cpp, BUG-0174) asks for the same centres and builds a `Set` (the host first, then
// every partner) for its tile passes and for which cells each guest gets. For the
// 0.8 Rivals mode (docs/11-rivals.md) every player plays its own nest far from the others: the
// same list is what keeps their areas alive on the host.
// The registry needs the game (view_centres.cpp); `Set` and `Box` below are portable (unit-tested in
// tests/cell_tests.cpp).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "net/protocol.h"

struct SkHost; // sporekit.h
struct SkMod;

namespace views {

struct Centre {
    net::Vec3 pos;
    uint8_t player = 0;
    uint32_t mode = 0; // the game mode the position was seen in
    double at = 0;     // when (H->time_seconds)
};

bool init(const SkHost* host, SkMod* mod);
// host: player `id` is at `pos` in our place, in game mode `mode` (a position that is not finite is
// not a centre: BUG-0292)
void note(uint8_t id, const net::Vec3& pos, uint32_t mode, double now);
void forget(uint8_t id);
void clear(); // the session ended
// the partners' centres seen in `mode` within the last `max_age` seconds; returns how many were
// written to `out` (at most `max`)
size_t partners(uint32_t mode, double now, Centre* out, size_t max, double max_age = 3.0);
// the view centre of player `id` seen in `mode` within the last `max_age` seconds
bool of(uint8_t id, uint32_t mode, double now, net::Vec3& out, double max_age = 3.0);
bool keep_alive_on(); // switch npc.view_centres
std::string status(); // coop.views

// ---- portable helpers (BUG-0174) ----

// at most this many centres in a Set (4 players today, room for 8): the cost of a stage's passes
constexpr std::size_t kMaxCentres = 8;

inline bool finite_pos(const net::Vec3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::fabs(p.x) < 1e7f &&
           std::fabs(p.y) < 1e7f && std::fabs(p.z) < 1e7f;
}

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

// the view centres one stage works with this frame: the host's own first (player 0), then the
// partners (from `partners()`), at most kMaxCentres, positions finite only
class Set {
public:
    void clear() { list_.clear(); }
    void set_host(const net::Vec3& p)
    {
        Centre c;
        c.pos = p;
        c.player = 0;
        if (list_.empty() || list_[0].player != 0)
            list_.insert(list_.begin(), c);
        else
            list_[0] = c;
    }
    // a partner's centre (replaces an older one of the same player); false when full or not finite
    bool add(uint8_t player, const net::Vec3& p)
    {
        if (!finite_pos(p))
            return false;
        for (auto& c : list_)
            if (c.player == player) {
                c.pos = p;
                return true;
            }
        if (list_.size() >= kMaxCentres)
            return false;
        Centre c;
        c.pos = p;
        c.player = player;
        list_.push_back(c);
        return true;
    }
    const std::vector<Centre>& list() const { return list_; }
    std::size_t size() const { return list_.size(); }
    const Centre* find(uint8_t player) const
    {
        for (auto& c : list_)
            if (c.player == player)
                return &c;
        return nullptr;
    }
    // the 2D distance to the nearest centre (a huge value without any)
    float nearest(float x, float y) const
    {
        float best = 3.0e38f;
        for (auto& c : list_)
            best = std::fmin(best, std::hypot(x - c.pos.x, y - c.pos.y));
        return best;
    }
    bool near_any(float x, float y, float r) const { return nearest(x, y) <= r; }

private:
    std::vector<Centre> list_;
};

} // namespace views
