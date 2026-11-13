---
id: BUG-1051
title: Tribe: a guest that joins another or a restarted host drops every Tribe snapshot as an older cycle (the cycle tracker is never reset)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-5
role: guest
versions: 
fixed_in: unreleased
branch: fix/w49-life
commits: f2f013ec, a49f379c, ade567a3, a7f75467, 398a5ab1
tests: tribe_cycle_restart, lifecycle_reset
signature:
docs: docs/known-issues.md#issue-tribe-cycle-not-reset, docs/01-architecture.md#arch-lifecycle-registry, docs/06-testing.md#testing-lifecycle-reset
related: BUG-0612, BUG-0616, BUG-0886
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-5 (the CycleTracker part). Not seen at runtime, found by code reading.
A guest that played Tribe with one host and then joins another host (or the same host after that host restarted its
game) without restarting its own game keeps the last snapshot cycle number of the first host. The new host counts its
cycles from 1, so every MSG_TRIBE_STATE chunk is "older" (within half the u16 range behind) and dropped: the guest sees
no host members move, no new mirrors, no removals, until the new host's counter passes the old one (about 30000 cycles
at 0.12 s = an hour, for a session of an hour before). `coop.status` shows `older_chunks=` growing.

## Root cause
Observed in code (main efd2e0ba), confidence high for the mechanism, the player effect inferred:
* mods/coop/tribe.cpp:424 `static net::CycleTracker g_cyc;` and :423 `g_cycle_ids` are never reset: neither
  tribe::on_session_end (tribe.cpp:5240) nor tribe::on_world_change (tribe.cpp:5271) calls `g_cyc.reset()`.
* tribe.cpp:3596 guest_state: `if (!g_cyc.begin(cycle, g_cycle_order_fix, &started)) return;` drops a chunk whose
  cycle is Older by net::cycle_order (mods/coop/net/validate.h:64: `(uint16_t)(cycle - current) >= 0x8000`).
* The host's counter tribe.cpp:311 `static uint16_t g_cycle;` starts at 0 in every game process (tribe.cpp:2541).
* Civ has the same tracker (civ.cpp:449) and resets it in civ::on_world_change (civ.cpp:4970), which a guest's join
  into the host's Civ world runs; civ::on_session_end does not, so Civ is only hit when no world change is seen
  between the sessions (an editor at the join, `civ.world` off). The audit's other WLD-5 items are in BUG-1055 or
  are not bugs (see the audit verdicts in docs/plan-0.7.14.md).

## Fix idea
Reset the tracker and the cycle's id set in tribe::on_session_end and tribe::on_world_change (and Civ's
on_session_end), as part of the lifecycle reset package (BUG-1055's registry). Switch idea:
`coop.fix tribe.cycle_reset off`.

## How to test
New twoinst test (e.g. tools/tests/coop_tribe_rejoin_host_restart.py, tribe fixture): host A and the guest play Tribe
for a while (or a test command sets the host's `g_cycle` to 40000), the guest leaves, the host's game is restarted (or
its counter set back to 1), the guest joins again. Assert on the guest's game that the host's members' mirrors move
(positions follow the host within the usual tolerance) and `older_chunks` stays 0. Main: no movement, older_chunks
grows: FAIL.

## Fix
The guest's Tribe cycle tracker and the cycle's id set are entry `tribe.cycle` of the lifecycle registry (session
end and world change; the drop counter `older` is kept for the status). Civ's tracker is entry `civ.cycle` (the session
end; civ::on_world_change reset it already). Switches `coop.fix tribe.cycle_reset off` / `civ.cycle_reset off`
(0.7.13). Test command `coop.tribe cycle <n>` (host) sets the snapshot counter (a long session, a restart).

## Verification
Test `tribe_cycle_restart` (coop_tribe_cycle_restart.py: the host's counter at 20000, the guest leaves, the counter
back to 1, the guest joins again; the guest's mirror must be where the host puts a member, 15 m away).
FAIL (SPORE_FIX_OFF=tribe.cycle_reset): out/lab/20261113-011952 (no mirror of the member at all, older_chunks 94 ->
176). PASS: out/lab/20261113-011705, out/lab/20261113-012647 (and out/lab/20261113-010644 with the counter at 40000, which
is "older" itself from the guest's side, 49 chunks dropped before the leave). Also in lifecycle_reset (tribe.cycle
held on the guest, none left after its resets).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-life (package L1, the lifecycle registry of BUG-1055).
