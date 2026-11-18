---
id: BUG-1147
title: Tests: the invariant monitor drops trace lines with a NaN position (-nan(ind)) or a state age of -0.0
status: fixed
kind: test
stage: All
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-tests
commits: 648fc446
tests: gates_selftest (parses own / partner line), space_allies
signature:
docs: docs/06-testing.md#testing-monitor-nowhere
related: BUG-1145
---

## Report
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Report
Found while planting a real separation for BUG-1145 into the logs of out/lab/20261115-051528 space_allies#1: the
guest's own lines in system 4046000C after the jump say `pos=(-nan(ind) -nan(ind) -nan(ind))` and the monitor dropped
them (its number pattern has no parentheses), so no invariant saw the guest from s 21 to s 69. A count over 3000
recorded game logs of all worktrees: 87 own lines with `-nan(ind)` and 4374 partner lines with `state_age=-0.0` (the
pattern took no sign) were dropped, besides the 540 own / 864 partner lines with `world=-` of BUG-1145.

## Root cause
Observed: OWN / PARTNER in tools/tests/monitor.py: `pos=\(([-\d.e+naif]+) ...` does not take MSVCRT's `-nan(ind)`,
`state_age=([\d.]+)` not `-0.0`; such a line is no record, and the invariants pair with a line up to 6 s away (the
BUG-1145 mechanism) or see nothing.

## Fix
One number token `N` (`[-\d.e+naif]+(?:\(ind\))?`) for pos and hp, a sign for state_age; `num()` makes `-nan(ind)` a
NaN as before. After it every own / partner line of the 3000 logs parses. `monitor.py report` over every recorded
passed result: one new violation, campaign_ct ground in fix/w45-village-ground 20261106-031300 (0.7.8: host's puppet of
partner 3 at agl -0.55 / -0.62 on two lines after an editor visit; the first of them had state_age=-0.0 and was
dropped before). That is the rule doing its job on an old build, not a false alarm.

## Verification
- no-fail-run: lab tooling (the patterns); gates_selftest has the four line shapes, which the old monitor fails and the
  new one parses; the PASS run out/lab/20261115-052922 of BUG-1145 (space_allies 5/5, lane 7) ran with it.

## History
- 2026-11-15: opened and fixed on fix/w51-tests.
- 2026-11-18: released in 0.7.15.
