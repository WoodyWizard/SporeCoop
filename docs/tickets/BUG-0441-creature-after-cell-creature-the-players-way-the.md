---
id: BUG-0441
title: creature: after Cell -> Creature the players' way the guest lacks 6 of the host's unlocked parts for the whole stage (parts.shared does not apply)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: transition_net, lab 20261101-180254 (feat/w33-tests-session, main 6ac351e)
role: guest
versions: main 6ac351e
fixed_in: 0.7.3.4
branch: fix/w36-parts-transition
commits: 27605dc 70c5e5f bf25d7b fac2bb6
tests: parts_transition (tools/tests/coop_parts_shared.py --from-cell), transition_net (allow removed)
signature:
docs: docs/re/creations.md#creations-cell-twins, docs/06-testing.md#testing-parts-transition
related: BUG-0401, BUG-0368, BUG-0446, BUG-0404
switch: parts.census_states
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
FAIL: SPORE_FIX_OFF=parts.census_states parts_transition, lab 20261101-195028 (lane 9: after the players'
Cell -> Creature guest 49 of 312 against the host's 55 of 324; the monitor's `unlocks` gate; every later
comparison 6 apart, also the guest's editor 51 against the party's 57 and after the rejoin). Also
20261101-190047 (an earlier version of the test).
PASS: parts_transition, lab 20261101-194722 (lane 9: host and guest 54:72501FA3 of 324 at once after the
transition; finds both ways in 1-2 s; the guest's editor offers 56:F33F0AF9 = the host's set, 268 locked;
after the rejoin 56 = 56).
Regression: lab 20261101-195534 (lane 9, gates on): transition_net (without the BUG-0441 allow), parts_shared,
guest_editor_locks, creations, guest_editor and smoke: 18/19 OK, `ui` skipped (exclusive, lanes 1-5 only).

## History
- 2026-11-01: opened.
- 2026-11-01: fix/w36-parts-transition: the census counts the item states too (parts.census_states); not a
  missing sync and not a host-only reward (the guest's game had the 6 parts unlocked); fixed (unreleased).
  The test found the host's Creature intro caption (cinematic 0A34E41F) waiting for its check after the
  transition, and a vote during it stuck the host's editor (BUG-0404, fixed on fix/w34-vote-cinematic).
- 2026-11-02: released in 0.7.3.4.
