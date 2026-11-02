---
id: BUG-0441
title: creature: after Cell -> Creature the players' way the guest lacks 6 of the host's unlocked parts for the whole stage (parts.shared does not apply)
status: open
kind: desync
stage: Creature
reported: 2026-11-01
source: transition_net, lab 20261101-180254 (feat/w33-tests-session, main 6ac351e)
role: guest
versions: main 6ac351e
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
`transition_net` (two players from the `cell` fixture; Cell -> Creature by the host's History button, the
evolution vote with the guest's real ready, the editors left with "done"; SPORE_NET=player, the guest at 30
fps; lab 20261101-180254, lane 9) failed by the invariant monitor's `unlocks` rule (feat/w32-monitor-diag):
`guest has 49 unlocked creature parts, the host had 55 60 s before` at s 140 and again at s 257. The trace
lines say `parts=55:D12B254B` on the host and `parts=49:FF9A58B8` on the guest for the whole Creature stage
(about 3 minutes, until the host went on to Tribe). Neither game logged a `parts:` line (no "part(s) of the
party's set unlocked here", no "sent ... to the host"), so BUG-0401's parts.shared did not act at all.

How to reproduce: `SPORE_LANE=N SPORE_NET=player SPORE_FPS=guest:30 python3 tools/tests/coop_transition_net.py
--to creature`, then `coop.parts` on both games (`sync: shared=... host_world=... have_set=...`).

## Root cause
Not analysed. Inferred from parts.cpp: `guest_tick` returns early unless the guest has the host's set
(`g_have_set`), `C.world()` equals the host's announced world (`g_host_world`, the sender's save CRC) and the
guest plays the host's world; after an in-game stage change both games have a new save (the host's share
after its evolution), so the world ids the host announces and the guest compares may not match. BUG-0401's
own test starts in the creature fixture, where they do.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
