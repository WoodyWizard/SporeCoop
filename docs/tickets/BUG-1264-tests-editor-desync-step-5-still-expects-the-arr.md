---
id: BUG-1264
title: Tests: editor_desync step 5 still expects the arrival vote and a guest in Creature (stale since BUG-1245 core.arrival_nowait)
status: open
kind: test
stage: Creature
reported: 2026-11-18
source: fix/w53-cellnow
role: host
versions: unreleased (rel/0.7.15.2)
fixed_in:
branch:
commits:
tests: editor_desync
signature:
docs:
related: BUG-1245, BUG-1240, BUG-0315
---

## Report
`editor_desync` fails at step 5 (Cell -> Creature): "its first editor waits for the vote" and "the guest follows". Seen on
fix/w53-cellnow (out/lab/20261118-135513) and on the base rel/0.7.15.2 without that branch (out/lab/20261118-134852):
not BUG-1263.

## Root cause
Observed (host / guest logs of 20261118-135513): the test does not answer the switch vote (its 60 s run out), then the
host's first Creature editor's arrival vote does not wait for the guest still on its way (BUG-1245 `core.arrival_nowait`:
"1 player(s) on their way to the Creature stage: not waited for"), so `vote active` is never seen. The guest arrives in
Creature 16 s after the switch and its own landfall editor opens 19 s later (BUG-1240), so `'Creature' in mode(guest)`
stays false for the test's 90 s wait, and the test's READY click lands in the guest's editor. The game behaves as
designed since BUG-1245; the step's expectations are stale (inferred: the step passed before BUG-1245 was merged,
rel/0.7.15.1 out/lab/20261118-110806).

## Fix
Open. Sketch: step 5 accepts the host's editor without a seen vote, waits for the guest in Creature or in its own
editor (leaving the guest's own editor with cancel as the step does later), and clicks READY only while a vote runs.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
