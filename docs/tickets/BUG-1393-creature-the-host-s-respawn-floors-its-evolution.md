---
id: BUG-1393
title: Creature: the host's respawn floors its evolution bar (36.9 -> 36.0), a guest's bar never follows the party's down: sync-check bar mismatch until the next gain
status: open
kind: desync
stage: Creature
reported: 2026-11-21
source: lab
role: 
versions: 
fixed_in:
branch: fix/w62-core
commits: 57941e27
tests: tools/tests/coop_dna_own.py (dna_own_setback, twin dna_own_setback_off), tests/dna_tests.cpp
signature: sync-check: MISMATCH ... (Creature ...): bar: host 36.0, guest 36.9
docs: docs/re/creature.md#creature-death-bar-floor
related: BUG-1291, BUG-1310
---

## Report
Third final check of 0.7.16 (lab 20261121-003202, nightly): mem_census FAIL on the sync-check gate only: "MISMATCH
with the host (Creature 68AC222A, 22 objects compared): bar: host 36.0, guest 36.9" (2 checks 3 s apart, s 465.5 on
the host). Every check of the test itself passed; mem_census had passed 2 of 2 before.

## Root cause
Mod (old in its 0.7.16 form: WP-D own DNA, BUG-1291, BUG-1310 compares the bar since 2026-11-19); not a w61 regression.
Observed in the run's logs: a creature's death and respawn drops that player's evolution bar to its whole number
(docs/re/creature.md#creature-death-bar-floor): the guest died at s 427 (36.9 -> 36.0) and the host's next resend
raised it back to the party's 36.9 within 1.5 s ("dna: own 37 -> 37, bar 36.0 -> party 36.9"); the host died at s
440.6 and respawned at s 449.8 with 36.0, the party's bar now, but `dna_own::apply_own` never lowers a guest's bar, so
the guest stayed at 36.9 until the next gain at s 496 (then raised to the party's 49.5). Earlier host deaths (19.2 ->
19.0) stayed within the compare's half point, so the test passed before by the bar's fraction at the moment of death.
Visible: the guest's bar up to almost a point ahead of the party's for minutes; no DNA was lost or doubled.

## Fix
`creature.bar_setback` (dna.cpp, dna_own.h apply_own, session_glue.cpp): a guest follows a fall of the party's bar
(the host's total's tail) of more than half a point and under one point (the setback of a death) through the game's
AddEvolutionPoints with the negative step, then sets its own DNA as before. A larger fall (a host's load of an older
save) is still not followed. The guest's log says "(the party's setback, BUG-1393)". Unit cases in
tests/dna_tests.cpp. New lab case `coop_dna_own.py --case setback` (the party's bar X.8, the host dies and respawns:
the guest's bar within half a point of the host's), twin `dna_own_setback_off`.

## Verification
Open, waiting for the lab run (no lab runs in this task):
- PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_setback mem_census --lane N --retry 0 --no-build`
- FAIL expected: `... run dna_own_setback_off ...` (the guest's bar stays 0.8 ahead).
- The FAIL run of the bug before the fix: 20261121-003202 mem_census.

## History
- 2026-11-21: opened.
- 2026-11-21: fix/w62-core: cause from mem_census's logs, fix creature.bar_setback, lab case setback.
