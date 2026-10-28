---
id: BUG-0154
title: ship, vehicle and tribe member ids can repeat after 65 535 allocations, and guests then mix up two objects
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#133
role: guest
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: a8341cf, 9afc74d
tests: tests/net_tests.cpp test_alloc_id
signature:
docs: docs/04-netcode.md#netcode-w2
related: #133
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/133 (full text, discussion).

The pattern of BUG-0023 (NPC net ids, `npc.cpp`) also exists in three more host-side id allocators: Space ships (`squadron.cpp`), Civilization vehicles (`civ.cpp`, two places) and Tribe members (`tribe.cpp`). Each one hands out `g_next_id++` as a `uint16_t` and wraps without checking whether the id is still in use. The counters are never reset during a session. After 65 535 allocations, a new object can get the id of a live one. The reverse map (`id -> object`) is then overwritten, both objects are sent under the same id, and the guest's mirror jumps between the two. Orders by id go to the wrong object. One of the civ allocators (`civ.cpp:1233`, a vehicle bought by a guest) does not even skip id 0.

This is theoretical in normal play: about 65k ships, vehicles or members in one session. It can happen in long sessions with many stage reloads, because every reload hands out new ids.

### Where
- Ships: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L200-L207 (`g_hid` / `g_hid_rev`)
- Civ vehicles found by the scan: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L983-L987 (`g_hv` / `g_hv_by_id`)
- Civ vehicle bought by a guest, no wrap check at all, so it can hand out 0: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1231-L1236
- Tribe members: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L418-L424 (`g_hm` / `g_hm_by_id`)
- Counters (file statics, never reset): `squadron.cpp:70`, `civ.cpp:125`, `tribe.cpp:154`.

### How to reproduce
No in-game reproduction (it needs 65k allocations). By reading: set the counter to 65535 in a debug build (`g_next_id = 0xFFFF`) while one object with id 1 is alive, then let one new ship or vehicle appear. The new object gets id 1 (the vehicle path at civ.cpp:1233 gets 0xFFFF, then 0) and the guest's mirror alternates between both positions.

### Expected
A live id is never handed out again. Id 0 is never handed out if 0 means "none" anywhere.

## Root cause
**What happens**

Example with ships:
- Ship A gets id 1 and stays alive (a planet's defender, for instance).
- After 65 535 further ships have come and gone, `g_next_id` wraps to 1 and ship B also gets id 1.
- `g_hid_rev[1] = B`: lookups by id (hits, orders) now reach B.
- Both ships go out in the snapshot as id 1. The guest's single mirror for id 1 gets both positions in one cycle, so it is drawn alternately at A and at B, and A's mirror on the guest is lost.
- Civ and Tribe behave the same with `g_hv_by_id` / `g_hm_by_id`: a guest's command for vehicle or member id X can move another player's object.

### Confidence
Confirmed by reading the code. A second reviewer verified it. Severity low (theoretical, about 65k allocations).

## Fix
Fixed on fix/w2-net. `net::alloc_id(next, in_use)` (mods/coop/net/ids.h) skips 0 and every id in
the reverse map, and returns 0 only when all 65 535 ids are taken. Used for ships (squadron.cpp,
`g_hid_rev`), civ vehicles (civ.cpp, the scan and the guest's purchase, which could hand out 0)
and tribe members (tribe.cpp, `g_hm_by_id`). The counters are not reset on a world change (not
needed once live ids are skipped). NPC ids (npc.cpp, BUG-0023 / BUG-0044) belong to the Creature
agent of wave 2 and can use the same header.

The review's suggestions were:

One small allocator in `net/` (portable, unit-tested), shared with the planned BUG-0023 fix. It skips ids present in the reverse map, never returns 0, and returns failure when all 65 535 ids are in use:

```cpp
template <class Map> uint16_t alloc_id(uint16_t& next, const Map& in_use) {
    for (uint32_t tries = 0; tries < 0xFFFF; tries++) {
        uint16_t id = next++;
        if (!next) next = 1;
        if (id && !in_use.count(id)) return id;
    }
    return 0;
}
```

Reset the counters on a world change, together with the tables.

## Verification
Unit test `test_alloc_id`: ids 1 and 2 alive, `next = 0xFFFF`: two allocations give 65535 and 3,
a full map gives 0. `--nofix` runs the old inline pattern: 65535 then 1, a live id (FAIL). No
in-game test (it needs 65k allocations).

Planned in the review:

`tests/net_tests.cpp` case `test_alloc_id`: fill a map with id 1, set `next = 0xFFFF`, and allocate twice. Assert the results are 0xFFFF and 2, never 1 or 0. This FAILS with the current inline `g_next_id++` pattern (copied into the test as the baseline) and PASSES with `alloc_id`.

### Related
BUG-0023 in docs/tickets ("NPC net ids can repeat after about 65k allocations in one world", same pattern in `npc.cpp`), #22 (a recycled NPC object keeps the old net id), #37 (vehicle and city tables survive a load), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #133.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
