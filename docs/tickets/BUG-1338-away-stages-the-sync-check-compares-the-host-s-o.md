---
id: BUG-1338
title: away_stages: the sync-check compares the host's own creature while the test makes its states report the menu, so the guest's hidden puppet is a mismatch
status: fixed
kind: test
stage: Space
reported: 2026-11-20
source: lab
role: host
versions: 0.7.16
fixed_in: unreleased
branch: fix/w59-tribe
commits: 9161752f
tests: away_stages, away_stages_override_off (coop_away_stages.py)
signature: gate sync-check: p: 1 missing on the guest (p0), while the host runs coop.hoststate mode 02CCD1D2
docs: docs/06-testing.md#testing-away-stages-override
related: BUG-0233, BUG-0378
---

## Report
Lab 20261119-214823 (0.7.16 final check), away_stages on lane 4: the test itself `RESULT: OK`, but the lab's sync-check
gate failed it: `sync-check: MISMATCH with player 1 'Bob' (Space 80460035, 2 checks 3 s apart, 1 objects compared): p: 1
missing on the guest (p0)` at 03:47:25.6 on the host (and the same on the guest).

## Root cause
✔ (logs) The test's second part makes the host's player states report the galaxy menu for 10 s (`coop.hoststate mode
02CCD1D2 10`, 03:47:15.9 .. 25.9); the host's game stays in Space. The guest's rule hides a partner in the menu
(`Alice hidden` at 03:47:15.9, "left for the main menu" at 17.3), so its digest has no p0, while the host's digest
lists its own creature (players_out leaves a player out only when its own state is dead, not in a world or a moving
pointer: the override changes only the state's mode). The two checks 3 s apart (22.6, 25.6) both fell into the
window: mismatch. `Alice appeared` again at 25.9. A gate that does not know the test's own fake: no mod bug.

## Fix
`diag.override_out` (coop.cpp players_out): while this game's states report another mode on purpose (the tests'
`coop.hoststate mode`, g_state_mode_override until its time), its own row is not compared (it goes into the digest's
`out` list, as a player out of the world does). Only that window, only that player; every other row is compared as
before. The test now asks for a sync-check inside the "menu" window (`coop.diag sync now`, 2.5 s in) and checks that
it finds no difference (the host's counters). Off (`SPORE_FIX_OFF=diag.override_out`, twin away_stages_override_off):
the check differs (then mismatches): FAIL.

## Verification
Lab: not run then (the 0.7.16 final check holds the lab): away_stages must PASS with the gate clean,
away_stages_override_off must FAIL (the sync-check of part 2 differs). Seen in the wild: 20261119-214823 away_stages.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): away_stages PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643 with the gate clean; the twin away_stages_override_off FAIL in 20261120-130608 (the sync-check differs). Closed.

## History
- 2026-11-20: opened (fix/w59-tribe, triage package G of the 0.7.16 final check).
- 2026-11-20: fix and test on fix/w59-tribe; lab runs pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
