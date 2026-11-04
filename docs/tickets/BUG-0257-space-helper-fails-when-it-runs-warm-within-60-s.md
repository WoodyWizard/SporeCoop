---
id: BUG-0257
title: space_helper fails when it runs warm within 60 s after a test that disabled the guest (space_stale)
status: fixed
kind: test
stage: Space
reported: 2026-10-28
source: 
role: 
versions: 0.7.5 and earlier
fixed_in: 0.7.6
branch: fix/w42-space-play
commits: 16d9960c, f8e809cb
tests: space_down_reload, space_helper
signature:
docs: docs/06-testing.md#testing-space-play-w42
related:
---

## Report
Seen in lab run 20261028-232331 (lane 3, fix/w5-space): `space_helper` ran warm right after `space_stale`, which disables the guest twice. The guest's lethal hit then destroyed its ship (`down=0 state=2`) instead of disabling it, and the host was disabled instead of destroyed. Inferred cause: `filter_own_damage` refuses a new disable within 60 s of the last one (`now - g_last_down < 60`), and the warm reset keeps the games (and that time) from the previous test. Not a game bug of this branch's changes (the same test passed cold on this branch: out/lab/20261028-235715). Fix idea: a test command that clears `g_last_down`, called by twoinst's warm reset or by the Space rescue tests at their start.


## Root cause
Observed (lab 20261104-111801, the fix off): the guest disabled, then within 60 s the warm reset (leave, the host
loads a fresh copy of the save, the guest joins and loads the host's world): the guest's first lethal hit next to
the host destroyed its ship (state 2). `filter_own_damage` refused a new disable while `now - g_last_down < 60`,
and that time survived the session end and the load. Not only a test problem: a player who was disabled and
rejoins (or the host loads) within a minute loses the ship to the first lethal hit.

## Fix
spacecoop.cpp: the 60 s count only within the Space world of the last disable (`spaceext::world_epoch`). Switch
`coop.fix space.down_epoch off`.

## Verification
- FAIL (space.down_epoch off): out/lab/20261104-111801.
- PASS: out/lab/20261104-111653; space_stale then space_helper warm on the same lane: out/lab/20261104-111920.

## History
- 2026-10-28: opened.
- 2026-11-04: released in 0.7.6.
