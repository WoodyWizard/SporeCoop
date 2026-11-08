// A guest's command that reaches the host while the host's game cannot run it (BUG-0693, class BUG-0423: credits and
// effects dropped or undone). Civ (MSG_CIV_CMD: a purchase, a planner building, a gift, a superweapon request) and
// Tribe (MSG_TRIBE_CMD) dropped such a command when the host was in an editor, a menu or a load: the guest clicked and
// nothing happened, or its planner building was gone at the next reconciliation. Now the host keeps it and runs it
// when its stage is back, in the same world (the save it played the stage in, modes::current_world: an editor
// visit and a reload of the same save keep it) and within kMaxAge; older ones and
// those of another world are dropped (counted). Portable: tests/validate_tests.cpp checks the rules.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace net {

struct HeldCommands {
    static constexpr size_t kMax = 32;     // commands held at most (the oldest goes first)
    static constexpr double kMaxAge = 120; // s: a command older than this is not run any more
    struct Cmd {
        uint8_t from;
        uint32_t world;
        double at;
        std::vector<uint8_t> bytes;
    };
    std::deque<Cmd> q;
    uint32_t held = 0, run = 0, dropped = 0;

    void push(uint8_t from, const uint8_t* p, size_t n, uint32_t world, double now)
    {
        if (q.size() >= kMax) {
            q.pop_front();
            dropped++;
        }
        q.push_back({from, world, now, std::vector<uint8_t>(p, p + n)});
        held++;
    }
    // the stage is back: run every command of this world not older than kMaxAge, in order
    template <typename F>
    void drain(uint32_t world, double now, F&& run_one)
    {
        while (!q.empty()) {
            Cmd c = std::move(q.front());
            q.pop_front();
            if (c.world != world || now - c.at > kMaxAge) {
                dropped++;
                continue;
            }
            run++;
            run_one(c);
        }
    }
    void forget(uint8_t from)
    {
        for (auto it = q.begin(); it != q.end();)
            it = it->from == from ? q.erase(it) : std::next(it);
    }
    void clear() { q.clear(); }
};

} // namespace net
