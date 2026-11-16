---
id: BUG-1221
title: Civ: after a reload a few of the guest's city citizens never bind to the host's (5 of 19 walk on their own; civ_citizens warm runs, also v0.7.14.1)
status: open
kind: desync
stage: Civ
reported: 2026-11-16
source: final check 0.7.15 (lab 20261116-025154 civ_citizens)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in:
branch: fix/w51-t4
commits:
tests: civ_citizens (coop_civ_citizens.py; warm runs)
signature: FAIL the guest's citizens are where the host's are: 40 of 152 without a partner (26%, at most 10 %)
docs: docs/06-testing.md#testing-final-triage-tribe-civ-0715
related: BUG-0476, BUG-0553, BUG-0475
---

## Report
Final check 0.7.15 (lab 20261116-025154, rel/0.7.15, lane 5, the 5th warm run of those games): `FAIL the guest's
citizens are where the host's are: 40 of 152 without a partner (26%, at most 10 %)`: in all 8 rounds 5 of the host's 19
citizens near the centre had no guest citizen within 15 m (the guest's 19 within 160 m). v0.7.14.1 (a detached
worktree of the tag, 0.7.15 tools) 20261116-093217 civ_citizens#4 (warm run 3): exactly the same line. Other runs of the
same commits: fix/w51-t4 20261116-082817-2 (2 cold PASS), -091255 (cold PASS, warm FAIL on the sizes: 4 of 27 pairs,
15%), -092515 (cold + 3 warm PASS); v0.7.14.1 -093217 (cold + 2 warm PASS). So a warm run (the host reloads the world
in the running games, the guest follows) now and then leaves a few citizens of each game without a partner for the
whole test. Players: after the host loads a save in Civ.

## Root cause
Observed (the guest's `coop.wobj` line at the end): failed runs have `kind citizen binds=15 own=4` (final check) and
`binds=17 own=2` (v0.7.14.1): 2-4 of the guest's citizens stay unbound ("own") and 2-5 of the host's citizens are
`missing` at every snapshot (missing 1300-1635 against 0-470 in passed runs). worldobj binds a host citizen only to a
free guest citizen within the kind's 30 m (`tol`); the citizen kind has no relocation, so a host citizen whose free
guest partner stands farther stays unbound, and that guest citizen walks on its own. Inferred: after the reload each
game's pool of citizens starts at other spots (BUG-0553: the pool goes to the city at each game's camera) and the
first binds pair them unevenly.
Tried (fix/w51-t4, not kept): the worldobj relocation for citizens (a host citizen without a copy within 30 m takes
the guest's nearest free citizen within 60 m, `kCitizenFar`, and puts it there; switch civ.citizen_rebind):
20261116-094048, 6 runs (1 cold, 4 warm, 1 cold after the warm ones): the place check passed in all 6 (0-4 % unpaired,
6-35 relocations), but the size check failed in 2 (11 %, 14 %: pairs with swapped sizes, 1.53 host / 1.30 guest), also
in the cold one; without it the size check failed in 1 of 12 runs. Its twin could not run warm (the lab starts a
SPORE_FIX_OFF run cold, 20261116-094707), so the FAIL / PASS pair the release rule wants was not possible either.
The try in code: the citizen kind with `.place = citizen_place` (game::stop, game::place at the host's position), `.relocate = kCitizenFar`, `.place_fix = &g_cit_rebind_fix` (worldobj.cpp, 23 lines).

## Fix
None (see the try above). Next: bind citizens after a reload by nearest pairs over the whole snapshot (not first
come within 30 m), and give a relocated citizen the host's size in the same frame; a warm-run twin in the lab (or a
`coop.load` of the fixture inside the test) to show it fail.

## Verification
* FAIL: 20261116-025154:civ_citizens (rel/0.7.15), 20261116-093217:civ_citizens#4 (v0.7.14.1), the size kind
  20261116-091255:civ_citizens#2 (fix/w51-t4).
* PASS: 20261116-082817-2 #1 #2, -091255 #1, -092515 #1-#4 (fix/w51-t4); -093217 #1-#3 (v0.7.14.1).

## History
- 2026-11-16: opened (fix/w51-t4, final-check triage 0.7.15): old (v0.7.14.1 the same), warm runs only; a relocation fix tried and dropped (sizes).
