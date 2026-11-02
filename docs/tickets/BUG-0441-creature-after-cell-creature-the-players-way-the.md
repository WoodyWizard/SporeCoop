---
id: BUG-0441
title: creature: after Cell -> Creature the players' way the guest lacks 6 of the host's unlocked parts for the whole stage (parts.shared does not apply)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: transition_net, lab 20261101-180254 (feat/w33-tests-session, main 6ac351e)
role: guest
versions: main 6ac351e
fixed_in:
branch: fix/w36-parts-transition
commits: 27605dc
tests: parts_transition (tools/tests/coop_parts_shared.py --from-cell), transition_net (allow removed)
signature:
docs: docs/re/creations.md#creations-cell-twins, docs/06-testing.md#testing-parts-transition
related: BUG-0401, BUG-0368, BUG-0446
---

## Report
`transition_net` (two players from the `cell` fixture; Cell -> Creature by the host's History button, the
evolution vote with the guest's real ready, the editors left with "done"; SPORE_NET=player, the guest at 30
fps; lab 20261101-180254, lane 9) failed by the invariant monitor's `unlocks` rule (feat/w32-monitor-diag):
`guest has 49 unlocked creature parts, the host had 55 60 s before` at s 140 and again at s 257. The trace
lines say `parts=55:D12B254B` on the host and `parts=49:FF9A58B8` on the guest for the whole Creature stage
(about 3 minutes, until the host went on to Tribe). Neither game logged a `parts:` line (no "part(s) of the
party's set unlocked here", no "sent ... to the host"), so BUG-0401's parts.shared did not act at all.

How to reproduce: `SPORE_LANE=N SPORE_NET=player SPORE_FPS=guest:30 python3 tools/tests/coop_transition_net.py
--to creature`, then `coop.parts` on both games (`sync: shared=... host_world=... have_set=...`).

## Root cause
Observed (lane 9, `coop.parts keys|states` on both games after the players' Cell -> Creature; Ghidra GOG 2024):
not a missing sync. The guest had the host's set (`have_set=1 host_set=55 host_world=9652F8AD`, same world,
rx_sets every 10 s) and nothing to apply. The 6 parts the guest's census lacked (02B3EDDB 7E262CF0
BE830BDE DEF1C7FF FE534766 3A3C49A5, type 0x40626000) are creature twins of Cell parts: the live Cell ->
Creature start 0xE83910 adds the 12 twins of table 0x1485FE8 to the unlockable map `+0x1490`
(AddUnlockableItem) and unlocks the twins of the Cell parts the cell had. The host made that start (map
324 items); the guest loaded the host's world, and a load rebuilds the map from the game's data without
them (312), but the item states `+0x4D04` came with the save: **both games' 324 states were identical**,
the 6 twins state 5 (unlocked) on the guest too. IsUnlocked (the palette's check 0x5C6820) reads the
state first, so the guest's editor offered them; the census (trace `parts=`, the monitor's `unlocks`
rule, `coop.editor`, and parts.shared's locked list) went over the map only: 55 against 49. A latent
real gap followed from it: parts.shared could never unlock a twin on a guest (not in its locked list).
docs/re/creations.md#creations-cell-twins.

Not a reward given to the host only (no C1 / BUG-0418 case): the guest's game has every part the host has.
The world ids matched (9652F8AD on both, the gate said Same): the inferred cause above was wrong.

## Fix
`parts.census_states` (parts.cpp `census`): the census goes over the map `+0x1490` and the item states
together (each key once), counting IsUnlocked: every game counts what its palette offers (host 55 of 324,
guest 55 of 324), the twins are in the locked list parts.shared unlocks from, and `coop.editor` counts
the same way (`available=<count>:<hash>`). Test commands `coop.parts keys` / `coop.parts states`.
New symbols (documentation): `SporeCoop::CreatureStart_FromCell`, `SporeCoop::CellTwinParts`.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
