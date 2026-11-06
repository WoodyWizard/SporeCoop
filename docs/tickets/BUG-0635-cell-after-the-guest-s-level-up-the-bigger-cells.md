---
id: BUG-0635
title: Cell: after the guest's level-up the bigger cells near it vanish and others appear (mirrors freed by the rebuild, made again)
status: open
kind: desync
stage: Cell
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-02, M0ng0r-03
role: guest
versions: 0.7.8
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0604, BUG-0511, BUG-0519
---

## Report
Discord report 2026-11-06 (0.7.8), discord-2026-11-06/M0ng0r-02 and M0ng0r-03 (the guest, mark at s 438): "after
growing, bigger cells near the guest disappear and new ones spawn".

## Root cause
Not BUG-0604 (fixed unreleased 0.7.9): that one is plain water (`screen: creatures=0` for minutes from a NaN camera).
Here the guest's screen keeps cells after its level-up at s 408.1 (`creatures=10`, then 5, then 2 at s 410 / 420 / 430)
and no NaN line appears in the log.
Observed (the guest's log): `[s 408.054] tiles: ... level 8; cell pool 237/4096` (442 at s 398), then the pool and the
mirrors churn (466/467 -> 473/475 -> 423/429). The guest levels up on its own each time (levels 6..10 at s 277, 368,
408, 528, 578).
Inferred: the guest's level change (RemoveCellsOfOldScale / RebuildLevel) frees the big mirrors nearby and the host's
next state makes them again at other places or sizes: cells vanish and reappear. Related: BUG-0511 (host scale in
MSG_CELL_STATE, fixed 0.7.4), BUG-0519 (puppet scale, fixed 0.7.5).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- Lab: cell_new_game; record the mirrors' host indices and positions around a guest level-up: ids that disappear
     and come back within a few seconds reproduce it. -->

## History
- 2026-11-06: opened. From the Discord report 2026-11-06 (0.7.8), the players' logs read in fix/w45-intake.
