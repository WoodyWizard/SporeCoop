// Portable bookkeeping of the shared resources (shared.cpp), kept apart from the game so that
// `make test` can check it (BUG-0106).
//
// A guest spends from its local copy and reports the change to the host (MSG_RES_ADD with a
// sequence number); the host counts it and echoes the last counted sequence of every guest in its
// next total (MSG_RES). Until then the change is "in flight": a host total that left before the
// host counted it must not undo it on the guest's screen (the money jumped back up for one round
// trip and could be spent a second time).
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace net {

struct SharedLedger {
    struct Sent {
        uint32_t seq;
        double v;
    };
    std::vector<Sent> in_flight; // reported, not counted by the host yet
    double pending = 0;          // changed locally, not reported yet
    uint32_t next_seq = 1;

    void reset()
    {
        in_flight.clear();
        pending = 0;
    }

    // the pending change goes out now: its sequence number for the message
    uint32_t send()
    {
        uint32_t s = next_seq++;
        in_flight.push_back({s, pending});
        pending = 0;
        return s;
    }

    // a local change the guest's tick has not sampled yet (made after its last tick, e.g. in the
    // frame a host total arrives): reported like any other change (BUG-0220). `synced` is the
    // game's value after the last write or sample; only valid while the guest has a host total.
    void fold(double cur, double& synced)
    {
        double d = cur - synced;
        if (std::fabs(d) > 1e-3) {
            pending += d;
            synced = cur;
        }
    }

    // the value to show for a host total. has_ack false: a host that does not echo sequences
    // (older build) - its totals are taken as counting everything sent (the behaviour before).
    double shown(double total, bool has_ack, uint32_t ack)
    {
        std::vector<Sent> keep;
        if (has_ack)
            for (auto& e : in_flight)
                if ((int32_t)(e.seq - ack) > 0)
                    keep.push_back(e);
        in_flight.swap(keep);
        double s = total + pending;
        for (auto& e : in_flight)
            s += e.v;
        return s;
    }
};

// the host counts a guest's change: the shared total never goes below 0 (the part a crossing
// spend could not cover is lost, the game already made the purchase)
inline double shared_add(double cur, double change, bool floor)
{
    double n = cur + change;
    return floor && n < 0 ? 0 : n;
}

// a player joined: does this machine forget its sync state (have = false)? The host does, so the
// newcomer gets every total at once. A guest keeps it (BUG-0220): it was reset for every other
// player's join, and a spend in the window until the next host total was undone on its screen
// and never reached the host. (The newcomer itself is reset while it joins: shared::tick.)
inline bool reset_on_join(bool host, bool keep_fix) { return host || !keep_fix; }

} // namespace net
