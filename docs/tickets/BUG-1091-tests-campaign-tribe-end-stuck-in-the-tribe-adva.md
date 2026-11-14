---
id: BUG-1091
title: Tests: campaign_tribe_end stuck in the Tribe advance, the host's raid click never selects a member (village 2's hut never falls in 30 min)
status: open
kind: test
stage: Tribe
reported: 2026-11-13
source: fix/w49-proxy lab 20261113-181333
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1073
---

## Report
fix/w49-proxy, campaign_tribe_end lab 20261113-181333 (lane 5, rel/0.7.14 + BUG-1089 / BUG-0440 diagnostics): the
Tribe advance never finished (findings: "[stuck] 18:52:26 tribe/advance: the stage change tribe -> civ did not finish
in 30 min (goal bar (1.0, 5.0))"). From 18:42 every raid click of the host's bot on village 2's hut came back "by []
(the host's click: no member selected (4-7 own))", spawned and ordered members did not fell the hut (750/750) in two
300 s tries. The same build passed the same test on lane 4 (lab 20261113-180327). Same picture as the "no member
selected" of BUG-1073 (cinematics flag 0 in Tribe); the host's trace has `busy=cinematic` in only 4 of 203 Tribe lines
(not checked whether the flag was 0). Not analysed (outside fix/w49-proxy's tickets).
Reproduce: `lab.py run campaign_tribe_end` (1 of 2 runs here).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
