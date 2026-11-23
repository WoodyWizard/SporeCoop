---
id: BUG-1383
title: cell_spin: the host removes the creature of a guest's lethal bite more than 1 s later, the test bites a third time and counts it as lost
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_spin
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w61-cell
commits: f0ba674a
tests: cell_spin
signature:
docs:
related: BUG-0001, BUG-0632
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 6, warm): cell_spin FAIL "every guest bite reached the host (hits_in
+2)", with the creature's hp on the host [6, 3, 3, -1] (three bites). First FAIL of this test (0 of 17 before).

## Root cause
Observed (game logs): guest s 66.741 `bite: sent c2848719310 dmg=3`, host `hp 6 -> 3`; guest s 67.764 `eat: sent break`
(the lethal bite on its mirror), host s 67.786 `kill c2848719310: applied` and `lethal through the game: hp 3 -> 3
anim=13`. The host's cell was still there with hp 3 when the test looked 1 s later (removed after its death), so the
test bit a third time; that bite hit the guest's broken mirror and sent nothing (the guest log has no third send). The
host got both bites the guest sent (hits_in +2 is right); the test counted three. Earlier runs read -1 within the 1 s.

## Fix
coop_cell_spin.py: after each bite the test waits up to 5 s for the host's answer (the hp changes or the cell is gone)
instead of a fixed 1 s. Checks unchanged.

## Verification
no-fail-run: test-only change (timing). Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_spin --lane N --retry 0
--no-build` PASS; `--without-fix` (mirror fixes off) still fails the guest's kill. Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_spin PASS in 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
