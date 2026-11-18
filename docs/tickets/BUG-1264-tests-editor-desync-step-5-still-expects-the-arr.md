---
id: BUG-1264
title: Tests: editor_desync step 5 still expects the arrival vote and a guest in Creature (stale since BUG-1245 core.arrival_nowait)
status: fixed
kind: test
stage: Creature
reported: 2026-11-18
source: fix/w53-cellnow
role: host
versions: unreleased (rel/0.7.15.2)
fixed_in: unreleased
branch: fix/w53-cellnow
commits: 06c53236 41915a85
tests: editor_desync
signature:
docs: docs/06-testing.md#testing-cell-editor-now
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
Test only (coop_editor_desync.py step 5): the host's first editor is accepted with or without a vote seen, the guest
"follows" when it is in Creature or in its own first editor (BUG-1240; it is cancelled later in the step as before),
READY is clicked only while a vote runs, and the host's creature key is read right after it entered Creature (read in
its editor it was empty, which let "the host plays its evolved creature" pass on any key).

## Verification
* FAIL (the old step): out/lab/20261118-135513 (fix/w53-cellnow) and rel/0.7.15.2 out/lab/20261118-134852.
* PASS: out/lab/20261118-141734 (lane 2, editor_desync OK, all steps).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-cellnow): the step follows the design of BUG-1245 / BUG-1240; fixed.
