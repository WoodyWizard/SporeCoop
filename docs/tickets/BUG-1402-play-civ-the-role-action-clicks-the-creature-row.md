---
id: BUG-1402
title: play_civ: the role action clicks the creature row's arrows in Civ (BUG-1289) and changes the guest's species
status: fixed
kind: test
stage: Civ
reported: 2026-11-21
source: lab 20261121-003202 (0.7.16 final check 3)
role: guest
versions: rel/0.7.16 21025f40
fixed_in: unreleased
branch: fix/w62-flake-tcs
commits: 5f8ec7c6
tests: play_civ
signature:
docs:
related: BUG-1289, BUG-1403
---

## Report
The third 0.7.16 final check (lab 20261121-003202, nightly, lane 3) failed play_civ with `FAIL no problems (1)`; the one
PROBLEM was `guest picked the role ?, the host has none` (at 01:52:42, 67 s into the session). The suite history calls
play_civ a known flake (17 of 62 on rel/main), but this PROBLEM is new in 0.7.16: none of the earlier play_civ FAIL runs
had it (their problems were rejected guest commands, ownership that did not arrive, a spice node; the late-October
campaign runs had "picked the role ?" from an older cause, before BUG-1289).

## Root cause
Observed (play_civ.guest.game.log of that run): at 01:51:30 `creations: we play the host's species here and have 5
creature(s) of our own: the co-op window offers them (BUG-1289)`; at 01:52:40 `ui: click cnext` three times, each followed
by `creations: own creature ... recorded for world BF000B7B` and `player 1 creature = ... 'Kirgle' / 'Ms Spoffit' /
'Chitis'`, then `species: player 1's line here: creature 7C530B3A tribe 7C530B3A civ 7C530B3A` and `player 1's Civ species
7C530B3A is baked here`. So the test's role action (play_civ.py a_role: F9 window, click `cnext` 1-3 times, read
"My role: X") clicked the arrows of the creature row: since BUG-1289 (`creations.late_species`, c29e3ba4, merged before the
w61 batch) a guest in Civ that plays the host's species and has own creatures sees "My creature: ... < >" in that row
(creations::late_row: while it plays no own creature here, and 30 s after a click), and the arrows pick a creature
(coop_ui.cpp g_row_creation), by design (coop_late_species asserts "the row picks a creature, not a role"). No "My role"
text: `want` was "?"; the host's `coop.role` had no role for player 1: the PROBLEM. The test also changed the guest's
species in the middle of the session as a side effect. Later role actions saw the role row again (the guest then played
its own creature), so only the first one failed. Whether it happens depends on the seed (the first a_role of the guest
while the row shows the creature): the second check's run 20261120-133703 had the same row (`... 5 creature(s) of our
own`) but no guest click on `cnext`. Not a w61 regression; not a mod bug in the role sync (the host never got a role
because none was picked).

## Fix
Test (play_civ.py a_role): the role action reads the row first and clicks the arrows only when the row is the role row
("My role: ..."); another row (the creature row of BUG-1289) is counted `role_other_row` and nothing is clicked. The role
check itself (the picked role on the host's `coop.role`) is unchanged for every role row. No mod change, no switch.
Coverage note: in the civ_npc fixture the guest has own creatures and plays the host's species, so while BUG-1403 holds
the guest's role actions are `role_other_row` (no guest role is picked in play_civ; the host's role actions and
civ_role_damage / coop_civ still cover the role sync).

## Verification
Was waiting for the lab run. FAIL run (old test): out/lab/20261121-003202 (play_civ, seed 907689703). Lab:
`SPORE_DETECTOR=20 python3 tools/lab.py run play_civ --lane N --retry 0 --no-build` (and the replay
`SPORE_LANE=N python3 tools/tests/play_civ.py --minutes 10.0 --seed 907689703`): PASS, and the guest log has no
`ui: click cnext` while the row shows the creature (counter `role_other_row` > 0 in the action line).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): play_civ PASS in 20261122-194210. Closed.

## History
- 2026-11-21: opened (fix/w62-flake-tcs): cause found in the guest log, test fixed.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
