---
id: BUG-1382
title: cell_away_editor: the guest's own plant meals while the host is away are counted against the +10 of coop.pool food (0 -> 22)
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_away_editor
role: 
versions: unreleased
fixed_in: 0.7.16
branch: fix/w61-cell
commits: e1ec5246
tests: cell_away_editor, cell_away
signature:
docs:
related: BUG-0205, BUG-0239
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 2, cold): cell_away_editor FAIL "the guest's game counts its meal
while the host is away (0 -> 22)" and "the 10 points eaten while the host was away are in the host's total, the guest
the same (0 -> 22, guest 22)". History: 4 of 14 failed on all branches since 2026-10-30; passed in 20261120-041304.

## Root cause
Observed (the test's own lines): before `coop.pool food 10` the guest's away line already said `kept_plant=12`: its idle
cell had eaten 2 plants of its own pool while the host was in its editor (6 points each); after the 10 points
`kept_meat=10 kept_plant=12`, and after the host's return `kept_sent=22` and the host's total 0 -> 22, the guest's 22.
So the mod did what BUG-0205 wants (every meal of the away time reached the host); the test counted the +10 from the
total read before the host left (g0 = 0) and wanted the host at exactly h0 + 10. A test fault.

## Fix
coop_cell_away.py: the +10 is counted from the guest's total right before `coop.pool food 10`; the host's total must
reach h0 + everything the guest ate while away (g1 - g0, the 10 points and its own meals; at least that, since the host's
cell may eat after its return) and the guest the same. The dip check is unchanged.

## Verification
no-fail-run: test-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_away,cell_away_editor --lane N
--retry 0 --no-build` PASS. The old twin (pool.away off) is retired (BUG-1138); a lost meal would still fail both checks.
Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_away_editor and cell_away PASS in 20261121-003202, 20261122-194210, 20261123-023643. Closed.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
