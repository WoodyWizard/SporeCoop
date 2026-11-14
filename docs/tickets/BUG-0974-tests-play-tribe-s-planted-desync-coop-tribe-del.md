---
id: BUG-0974
title: Tests: play_tribe's planted desync (coop.tribe delay 0.6) no longer delays the guest's mirrors, the test passes with it
status: fixed
kind: test
stage: Tribe
reported: 2026-11-12
source: feat/lab-speed lab 20261112-174746 and 20261112-174029
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-tests
commits: 5bad62c6
tests: play_tribe
signature:
docs: docs/06-testing.md#testing-w49-tests
related: BUG-0481, BUG-0425
---

## Report
docs/06-testing.md (the play_* bots' chronic failures, fix/w40-play) names the planted desync of play_tribe:
`PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6"` (the guest's mirrors 0.6 s late) fails `sync t*: 90% within 1.5 m`
(p90 2.71 m, best lag 0.60 s, out/lab/20261103-152720). Checking that the quick mode of feat/lab-speed still catches it,
play_tribe PASSED with the plant both in the quick run (5 min, lab 20261112-174029) and at full length (10 min, lab
20261112-174746): the guest printed `coop.tribe delay 0.6: mirrors follow the host's states 0.600000 s back`, but the
probe saw `best lag p50 0.00 s` / `0.01 s` walking, citizens p90 0.47 m. So the test has no FAIL case any more: the
command is accepted and does nothing to the mirrors the test measures (or the test reads something the delay does
not reach). Not a player bug as far as known.

## Root cause
Not looked into (not feat/lab-speed's task). Inferred: the mirrors' driving changed after 0.7.4 (the lead / drive
work of 0.7.9-0.7.11, tribe mirror lag) and no longer reads the delay `coop.tribe delay` sets.
Observed in code (fix/w49-tests): the check that should catch it is play_tribe's `sync t*: 90% within 1.5 m of the
host's path over 0.32 s` (unchanged and right). The plant stopped working on 2026-11-06 with BUG-0425 (de9c4d8d,
`drive.lead`): `coop.tribe delay` only sets `SnapshotBuffer::delay` of each member mirror (tribe.cpp guest_state),
and with `lead` on, `sample_raw` (net/interp.h) shows the sender's state now (+ the one-way latency) and reads `delay`
only for a jittery link; the planted 0.6 s went nowhere and the mirrors stayed on the host's path (best lag 0.00 s).
The plant had been made (BUG-0481, 2026-11-03) three days before the lead.

## Fix
`net::SnapshotBuffer::test_lag` (tests only): with the lead, the target time is held back by it; tribe.cpp sets it
from `coop.tribe delay` (a value other than the normal 0.2 s). The lead-off path is unchanged (`delay` does it). No
player path changes (0 unless the test command ran). Switch `tribe.test_delay_lead` (off: the plant does nothing, as
in 0.7.13).

## Verification
play_tribe on lane 5 (fix/w49-tests-cpp worktree, merged into fix/w49-tests), the plant
`PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6"`:
* FAIL with the plant, `--quick` (5 min): out/lab/20261113-050258 (`sync t*` FAIL: walking p90 on the path within
  0.30 s 2.85 m, best lag p50 0.45 s, the probe's limit; raw p90 5.40 m).
* FAIL with the plant, full length (10 min): out/lab/20261113-051512 (`sync t*` FAIL, citizens p90 2.65 m, raw 5.30 m;
  the log gate's "1 NPC proxies over 3 m off" also failed in that run).
* PASS without the plant: `--quick` out/lab/20261113-050904 (p90 0.37 m, best lag 0.01 s), full out/lab/20261113-052641
  (p90 0.42 m).
* The plant with `SPORE_FIX_OFF=tribe.test_delay_lead` (as in 0.7.13): PASS, out/lab/20261113-053810 (best lag 0.02 s,
  p90 0.46 m raw): the twin shows the plant was what stopped working, not the check.

## History
## How to test
`PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6" tools/lab.py run play_tribe --lane N` must FAIL (`sync t*`); a fix
restores the delay or gives play_tribe another planted desync.

## History
- 2026-11-12: opened (feat/lab-speed).
- 2026-11-13 (fix/w49-tests): root cause: the lead (BUG-0425) never reads the buffer's delay; the plant now holds
  the lead target back (tribe.test_delay_lead); FAIL with the plant on --quick and full, PASS without.
- 2026-11-14: released in 0.7.14.
