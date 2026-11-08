# SporeCoop 0.7.11 plan (2026-11-06, starts right after 0.7.10 is released)

Moved from docs/plan-0.7.10.md by the owner's decision of 2026-11-06 (0.7.10 is kept short: the Discord reports of
2026-11-06, the address space, the remote bodies). Same rules as docs/plan-0.7.10.md, including: every ticket of this plan
closes in this release, fixed or with evidence after a serious dedicated attempt; "not reached" is not an outcome.

## Step 0: intake

* New player reports since 0.7.10: a ticket each, into the packages below (or a new P package first).
* What 0.7.10 leaves open, with its evidence, goes into the package of its stage.

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **P4 Creature** | A guest's avatar found ~14 m from where it joined, the host's creations on the guest, the previous creature version after the host's editor, the DNA budget message of a held editor request, the guest's editor showing the host's new parts, a chat line lost in play_creature | BUG-0333, 0629, 0200, 0457, 0581, 0283 |
| **V2 Civ and Space** | A guest's vehicle gift lost, vehicles of owner 0 after the chunk budget drop, the Space sync mismatch during a rescue, the zoom after a load, the ships' p90 near its limit, the Civ money row of the detector | BUG-0325, 0206, 0547, 0182, 0190, 0613 |
| **K2a Classes: state** | State sent but not applied, hits and world state without a relation / world filter, per-player state under a reused id, credits and effects dropped or undone; a relayed effect at the sender's ground height | classes BUG-0419, 0420, 0422, 0423; BUG-0608 |
| **K4 Remote bodies under spikes** (found in 0.7.10 K3) | A body extrapolated through a latency spike while the host turns (members to 8-9 m, NPC proxies teleport); a thrown NPC's copy waits for every state on a player network; the guest still without lead under heavy load | BUG-0674, 0675, 0651, 0680 |
| **K5 Space shots** (found in 0.7.10 S1) | The beam shots of the ships the host's game runs are not seen by a guest (mirror ships never fire, only Civ shots are relayed) | BUG-0677 |
| **K6 Space kept hold, the rest** (found in 0.7.10) | The limits of space.keep_items: a host restart between leave and join, changes while away, cargo slot count, non-spice cargo the host's copy lacks | BUG-0679 |
| **K7 Final-check findings of 0.7.10** | Caught faults and freezes the 0.7.10 final check found in code older than 0.7.10: the city planner's freed editor at the Space leaving frame, puppets under the ground in the brain-upgrade caption, a Civ tribe village faulting on a bundle the mod took, the host freezing on a share save after a rejoin, the monitor's Cell drawn invariant in two scales | BUG-0682, 0683, 0684, 0687, 0681 |
| **K2b Classes: content and limits** | Content the other game lacks, game rules copied by hand, mod input read globally, requests the mod builds incompletely, fixed-size game pools overrun, avatar-only rules a guest misses | classes BUG-0424, 0428, 0429, 0431, 0432, 0418 |
| **N1 Not reproduced, crashes and freezes** (dedicated) | One more serious attempt each on the current build (targeted tests, 4 players where the report had them, low fps / bad net): fix, or close with the runs as evidence | BUG-0026, 0118, 0167, 0170, 0171, 0172, 0184, 0578 |
| **N2 Not reproduced, desyncs and bugs** | As N1 | BUG-0011, 0013, 0178, 0221, 0237, 0282, 0328, 0402, 0316, 0528, 0359 (if 0.7.9 leaves it) |
| **E1 Features without Rivals** | The Cell -> Creature switch vote, the host's editor opening at once after the evolution vote | BUG-0315, 0516 |
| **G4 Tests** | Flaky and missing tests: each test made reliable (the cause found, not a looser limit) or the missing test written; the harness that hides failures | BUG-0021, 0022, 0168, 0177, 0189, 0181, 0193, 0293, 0300, 0311, 0363, 0450, 0489, 0430, 0199, 0628 |

A class umbrella closes when its children are fixed or closed and the detector shows no row of the class on any stage;
what is left becomes a concrete ticket in the same package.

## Order

1. Step 0, then wave 1 of at most 3 lab agents at once: P4, V2, K2a (then N1 as a lane pair frees up).
2. Wave 2: K2b, N2, E1, G4; after each report, unreached tickets go to a dedicated agent at once.
3. Integration run on rel/0.7.11 after every two merges.
4. Final check on at most 4 lanes: smoke, every test added in 0.7.10 and 0.7.11, play_* of all stages, campaign_*,
   detect_*, the long-session bot on Creature and Tribe. Then the release procedure.
