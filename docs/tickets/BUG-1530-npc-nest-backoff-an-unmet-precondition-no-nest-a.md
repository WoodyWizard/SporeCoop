---
id: BUG-1530
title: npc_nest_backoff: an unmet precondition (no nest animal backs off in time) fails the test instead of giving INCONCLUSIVE
status: fixed
kind: test
stage: Creature
reported: 2026-11-23
source: plan
role: test
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w76-motion
commits: a08dfd8b
tests: npc_nest_backoff, npc_nest_backoff_near
signature:
docs:
related: BUG-0562, BUG-1360
---

## Report
The npc_nest_backoff test fails when its precondition is not met (no nest animal backing off in time) instead of saying INCONCLUSIVE through the test helper's inconclusive path (as civ_planner_miss). Needed for the BUG-1360 mound `place` precondition. docs/plan-0.7.17.md items 10 and 12.

## Root cause
Observed (lab 20261123-023643 and -071312, rel/0.7.16 fd28f88f, tools/tests/coop_npc_nest_backoff.py): the test FAILed on
"the host's game has an NPC of that nest inside its mound" alone, while both real checks passed (no proxy drawn
inside the guest's surface, every proxy within 4 m of the host's NPC). The host's game had its nest NPCs 0.2 m inside
the surface at most (#13 -0.18 m for the first 9 s, the others -0.06 to +0.04 m), so the case of BUG-0562 (a nest
animal backing off into the mound while the host's game has no mound there) never happened and nothing was tested.
The test treated its preconditions (wild NPCs at a nest, the NPCs backed away, one in the host's mound, it on the
guest's screen) as checks: an unlucky roar or nest placement read as a mod bug (7 of 28 runs on all branches).

## Fix
2026-11-23 (fix/w76-motion), test only: the four preconditions (wild NPCs at a nest of the fixture; the NPCs backed
away over 1.5 m; without --host-near an NPC of that nest over 0.3 m inside the host's surface for over 1 s; the NPC
deepest in the mound on the guest's screen for 3 s) are no checks any more: unmet, the test ends with
`RESULT: INCONCLUSIVE <the first unmet one>` (twoinst.Checks.inconclusive, as civ_planner_miss); a real check that
failed (drawn inside the surface, over 4 m from the host's NPC, a fault) still makes it a FAIL. The real checks run
first, the preconditions are printed as `precondition not met: ...` or `ok   the case happened: ...`. No game code,
no switch (test-only change).

## Verification
no-fail-run: test-only change (a precondition now gives INCONCLUSIVE instead of FAIL); no switch, no twin.
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run npc_nest_backoff npc_nest_backoff_near
--repeat 3 --lane N --retry 0 --no-build` must show no FAIL on a precondition line (PASS, or INCONCLUSIVE naming the
unmet precondition); pending night run.
0.7.17 release lab: PASS: npc_nest_backoff (lab 20261124-020506), npc_nest_backoff_near (lab 20261124-020506).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-F1), branch fix/w76-motion.
- 2026-11-23 (fix/w76-motion): preconditions give INCONCLUSIVE; root cause from lab 20261123-023643 / -071312.
- 2026-11-24: released in 0.7.17.
