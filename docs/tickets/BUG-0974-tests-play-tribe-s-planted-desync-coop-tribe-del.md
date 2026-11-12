---
id: BUG-0974
title: Tests: play_tribe's planted desync (coop.tribe delay 0.6) no longer delays the guest's mirrors, the test passes with it
status: open
kind: test
stage: Tribe
reported: 2026-11-12
source: feat/lab-speed lab 20261112-174746 and 20261112-174029
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
## How to test
`PLAY_TRIBE_GUEST_CMDS="coop.tribe delay 0.6" tools/lab.py run play_tribe --lane N` must FAIL (`sync t*`); a fix
restores the delay or gives play_tribe another planted desync.

## History
- 2026-11-12: opened (feat/lab-speed).
