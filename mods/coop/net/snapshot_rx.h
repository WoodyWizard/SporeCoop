// C4 of docs/plan-0.7.15.md (audit STG-2): one receiver for the host's snapshots sent in chunks of one cycle, the
// code tribe.cpp guest_state, civ.cpp guest_state and squadron.cpp guest_state each carry today (with BUG-0612's
// older-cycle drop, BUG-0037's "a removal pass needs every chunk", BUG-1051's reset with the session and the world):
//
//   rx.begin(cycle, chunk, chunks)   a chunk's header: false = drop the chunk (a chunk number that cannot belong to
//                                    the cycle, more chunks than the policy allows, a late chunk of an older cycle).
//                                    A newer cycle starts empty (a new generation; the one before, if it never
//                                    completed, is counted `abandoned`).
//   rx.saw(id)                       an entry of the chunk was read (before its fields are applied, as today)
//   rx.end(parsed_ok)                the chunk was read to its end: true once per cycle, on the chunk that makes the
//                                    cycle complete (every chunk 0..chunks-1 in; see complete_on_last). A cut chunk
//                                    (parsed_ok false) does not count: the cycle stays incomplete, nothing is removed.
//   rx.finish(registry, decide, remove)   then: the mirror registry (C1, mirror_registry.h) sweeps every entry the
//                                    cycle did not name (mirror::Registry::sweep with rx.seen as the "seen" rule).
//
// Policy (one per stage): max_chunks (tribe / civ 255, the u8 count; squadron 31, its mask), keep_order (the switch
// net.cycle_order, BUG-0612: off, a late older chunk restarts that cycle as in 0.7.8), older_window (how far behind
// the current cycle a chunk counts as older: 0x8000, half the u16 range, for tribe / civ (validate.h cycle_order);
// squadron 99 (its `age > -100`: farther back is a restarted host's counter and starts a new cycle)), sweep_once (the
// completing chunk ends the cycle; a duplicate of one of its chunks later does not sweep again; squadron's g_rx_done),
// complete_on_last (tribe / civ: only the last chunk completes a cycle; squadron: any chunk that fills the mask).
// Portable (no Windows, no SporeKit): tests/core_tests.cpp checks it natively. docs/01-architecture.md#arch-snapshot-rx.
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace net {

struct SnapshotPolicy {
    uint16_t max_chunks = 255;     // chunks of one cycle at most (chunk < chunks <= max_chunks)
    bool keep_order = true;        // a late chunk of an older cycle is dropped (BUG-0612)
    uint16_t older_window = 0x8000; // a cycle up to this far behind the current one is older
    bool sweep_once = true;        // end() answers true once per cycle
    bool complete_on_last = true;  // only the cycle's last chunk can complete it (tribe / civ); false: any chunk that
                                   // fills the set (squadron's mask: a late chunk 0 after the last one completes it)
};

template <class Id>
struct SnapshotReceiver {
    SnapshotPolicy policy;

    // the cycle being received
    uint16_t cur = 0;
    bool have = false;      // a cycle was seen (cur is valid)
    bool done = false;      // cur completed (its sweep was handed out)
    uint8_t chunks_of_cur = 0;
    uint64_t got[4] = {0, 0, 0, 0}; // chunk bits 0..255 of cur
    uint32_t generation = 0;        // +1 per cycle started (local, never reset: a stamp for "this snapshot")
    std::unordered_set<Id> seen_ids; // ids cur named so far

    // counters (coop.status / the stage's debug line; reset() keeps them)
    uint32_t chunks_in = 0;   // chunks accepted
    uint32_t bad = 0;         // chunk headers that cannot belong to a cycle
    uint32_t older = 0;       // late chunks of an older cycle dropped
    uint32_t cut = 0;         // chunks read only in part (not counted)
    uint32_t incomplete = 0;  // last chunks that came while an earlier chunk of the cycle was missing
    uint32_t abandoned = 0;   // cycles a newer one replaced before they completed
    uint32_t complete = 0;    // cycles completed (sweeps handed out)

    explicit SnapshotReceiver(SnapshotPolicy p = {}) : policy(p) {}

    // a chunk's header. False: drop the chunk (nothing of it is applied)
    bool begin(uint16_t cycle, uint8_t chunk, uint8_t chunks)
    {
        if (!chunks || chunk >= chunks || chunks > policy.max_chunks) {
            bad++;
            return false;
        }
        bool newer = !have;
        if (have && cycle != cur) {
            uint16_t behind = (uint16_t)(cur - cycle);
            bool is_older = behind != 0 && behind <= policy.older_window;
            if (is_older && policy.keep_order) {
                older++;
                return false;
            }
            newer = true;
        }
        if (newer) {
            if (have && !done)
                abandoned++;
            cur = cycle;
            have = true;
            done = false;
            chunks_of_cur = chunks;
            got[0] = got[1] = got[2] = got[3] = 0;
            seen_ids.clear();
            generation++;
        }
        chunk_ = chunk;
        chunks_ = chunks;
        in_chunk_ = true;
        return true;
    }
    // an entry of the chunk being read
    void saw(const Id& id) { seen_ids.insert(id); }
    bool seen(const Id& id) const { return seen_ids.count(id) != 0; }
    // the chunk was read: true when this chunk completes the cycle (the caller sweeps now)
    bool end(bool parsed_ok)
    {
        if (!in_chunk_)
            return false;
        in_chunk_ = false;
        if (!parsed_ok) {
            cut++;
            return false;
        }
        chunks_in++;
        got[chunk_ >> 6] |= 1ull << (chunk_ & 63);
        bool last = chunk_ + 1 == chunks_;
        if (policy.complete_on_last && !last) // tribe / civ: only the sender's last chunk ends a cycle
            return false;
        if (count() < chunks_) {
            if (last)
                incomplete++;
            return false;
        }
        if (done && policy.sweep_once)
            return false;
        done = true;
        complete++;
        return true;
    }
    unsigned count() const
    {
        unsigned n = 0;
        for (uint64_t g : got)
            for (uint64_t x = g; x; x &= x - 1)
                n++;
        return n;
    }
    // the hand-off to the mirror registry (C1): every entry this cycle did not name is decided and swept
    template <class Registry, class Decide, class Remove>
    size_t finish(Registry& reg, Decide&& decide, Remove&& remove) const
    {
        return reg.sweep([this](const Id& id) { return seen(id); }, decide, remove);
    }
    // a new session or world (BUG-1051): the next chunk starts a cycle of its own; the counters stay
    void reset()
    {
        cur = 0;
        have = false;
        done = false;
        chunks_of_cur = 0;
        got[0] = got[1] = got[2] = got[3] = 0;
        seen_ids.clear();
        in_chunk_ = false;
    }
    // what is held (lifecycle.h `held`)
    size_t held() const { return (have ? 1 : 0) + seen_ids.size(); }

private:
    uint8_t chunk_ = 0, chunks_ = 0;
    bool in_chunk_ = false;
};

} // namespace net
