---
id: BUG-0386
title: Tribe: a guest's walking mirror held back by its own game (an obstacle?) falls 3.6 m behind and lurches 1 m per frame to catch up
status: open
kind: desync
stage: Tribe
reported: 2026-11-01
source: found by tribe_walk_mobile (fix/w30-tribe-walk)
role: guest
versions: unreleased
fixed_in:
branch:
commits:
tests: tribe_walk_mobile (tools/tests/coop_tribe_walk.py)
signature:
docs:
related: BUG-0365
---

## Report
tribe_walk_mobile (out/lab/20261101-091623, tribe_walk_mobile#2, lane 3, both games at 30 fps, mobile
network, with BUG-0365's tribe.mirror_walk on): the guest's mirror of its own member #7, walking at 9 m/s
in a group, fell behind its target from 0.1 m to 3.6 m within 0.8 s while its drawn step shrank to
0.11-0.22 m per 32 ms frame (the placing allows 0.38 m) and its game speed rose to 20 m/s; then it was
released and moved 1.09, 0.92 and 0.88 m in three frames (27-34 m/s) and caught up within 0.25 s. The
other mirrors were 9 m away. The per-frame track: out/lane3/track/tribe_walk_guest_walks_091855.json
(not in git). Seen once in 14 direction-views with the fix.

## Root cause
Not found. Inferred: something in the guest's own game (a rock, a tree, a building whose place differs
from the host's, or the citizen's collision) pushes the placed mirror back every frame (game::place
teleports it, the next update moves it back), until it is free and the collision resolution plus the
catch-up move it on fast. Not checked: what it collided with.

## Fix
Open. Ideas: find the blocker (coop.targets objs at that moment); cap the catch-up after a block; take
mirrors out of the guest's collision.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
