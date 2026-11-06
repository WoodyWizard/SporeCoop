---
id: BUG-0624
title: Civ: a city the host founds instantly (campaign shortcut) and its vehicles appear on the guest 4-7 s later (sync-check v missing at low fps)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-06
source: lab rel/0.7.9 20261106-105201 civ_city_layout (sync-check gate), INT3 reruns 20261106-122407
role: guest
versions: 0.7.9 1e155b53
fixed_in: unreleased
branch: fix/w45-int3
commits: 56a7ee58
tests: civ_city_layout, civ_city_burst
signature:
docs: docs/re/civ.md#civ-instant-found
related: BUG-0380, BUG-0319, BUG-0602
---

## Report
rel/0.7.9 1e155b53 integration run 20261106-105201: civ_city_layout itself OK, the lab gate failed it:
`sync-check: MISMATCH with the host (Civilization BF000B7B, 8 objects compared): v: 1 missing on the guest (v12)`.
On this tree (fix/w45-int3, 1e155b53 + rel/0.7.9) 3 of 3 runs failed the same way (lab 20261106-122407, v5).

## Root cause
Observed (game logs, docs/re/civ.md#civ-instant-found): the test founds three NPC cities on the host with
`SpawnNextCity(ex, 1, 0)` (instant, as the campaign's shortcut), which the co-op hook did not broadcast (only
`!instant && !who`). The guest found them through the catch-up only: 2 s short, then SpawnNextCity with the
appearance timer; at ~25 fps (a loaded PC: host `fps avg 22-27`) the host's three cities appeared 2 s apart
(`wait.frames 30` between the foundings) and each appeared on the guest 4-5 s after the host (terrain stamp logs:
host 37.2 / 39.3 / 41.3 s, guest 42.3 / 44.5 s), so the vehicle of a new nation was missing in two sync-checks 3 s
apart. At 60-69 fps (fix/w45-placement's own runs) all three fell in one snapshot and the window was shorter: it
passed. Not caused by a 0.7.9 merge: 46b4e1d5 (before fix/w45-village-ground) failed 1 of 3 under the same load
(lab 20261106-123450). Inferred: players do not found instantly in normal play (the game's AI founds with the timer),
so the visible effect is the campaign shortcut and tests.

## Fix
`civ.instant_found` (civ.cpp, 56a7ee58): the host's SpawnNextCity hook broadcasts an instant founding too
(MSG_CIV_BUILD kNextCitySite with noun 1; 0 stays the timer founding), a guest founds it instantly at once; the
catch-up founds instantly too (the city is late already). Off: not broadcast, the catch-up with the timer (0.7.8).
Older guests ignore noun 1 (no city index 0xFF) and keep the catch-up.

## Verification
FAIL (before the fix, the switch's off side): lab 20261106-122407 (3 of 3, civ_city_layout, v5 missing),
20261106-123450 (46b4e1d5, 1 of 3). With `SPORE_FIX_OFF=civ.instant_found` lab 20261106-125116 passed 2 of 2 at a
lower load (the catch-up path is in its log: "founding 3 of 3 at once", the sync-check did not land in the window).
PASS: lab 20261106-124414-2 (3 of 3 at fps 19-50; guest log "the host founded a city: here founded" for each
instant founding).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int3): fixed (civ.instant_found), found by the third integration run of 0.7.9.
