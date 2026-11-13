---
id: BUG-1061
title: Creature: after a session in Cell, the partner's puppet shows its own species' full health instead of the partner's for 10+ s after the next Creature join
status: fixed
kind: test
stage: Creature
reported: 2026-11-13
source: fix/w49-life lab (coop_lifecycle_reset.py, monitor puppet_hp)
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-small
commits: f4aca716
tests: lifecycle_reset, gates_selftest
signature:
docs: docs/known-issues.md#issue-puppet-hp-after-cell, docs/06-testing.md#testing-t1-small
related:
---

## Report
Seen by the invariant monitor (rule puppet_hp) in 3 of 4 runs of coop_lifecycle_reset.py on fix/w49-life (lane 4):
the two games played Cell (fixture cell), both left the session, the host loaded the creature fixture with
`coop.load`, hosted again and the guest joined (it loaded the host's world). At the 30 s mark the monitor saw
`guest shows host's creature at 10/10, host's own game 6/6` (out/lab/20261113-010644-2, 20261113-011601) and
`host shows guest's creature at 18/18, guest's own game 6/6` (out/lab/20261113-011935): the puppet carries its own
full health, not the partner's (creature.partner_hp, BUG-0373), on two lines in a row. Also with
`core.lifecycle_reset off` (011601), so not from the lifecycle registry. Not seen in a Creature session that did not
follow a Cell one (the cold-start creature tests, the lifecycle test with Creature first: 20261113-011223).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not a game desync (observed in the logs of the reported runs): in out/lab/20261113-010644-2 (fix/w49-life) the host's own
Creature line was hp=10/10 and the guest showed it 10/10; in 20261113-011935 the guest's own line was 18/18 and the host
showed it 18/18. The monitor's Timeline sorts every own line of a machine by the session clock, which starts at 0 in
every session: the guest's Creature partner line at s 30.011 was matched with the host's own line of the Cell session
before it at s 30.017 (6/6, closer in time than the Creature line at s 30.026). The hp rule had the same matching.

## Fix
tools/tests/monitor.py: Timeline.window / own_at take the partner record and keep only own lines of the stage and
world it names; used by same_moment (puppet_hp, drawn, ground) and the hp rule. Config `same_session` (default 1;
0: the matching before).

## Verification
FAIL: the reported runs (fix/w49-life out/lab/20261113-010644-2, 20261113-011601, 20261113-011935: MONITOR-VIOLATION
puppet_hp); `monitor.py` replay of 20261113-011935 with same_session=0 gives the violation, with 1 none. gates_selftest
twin `f_php_sessions` (same_session=0 fails, 1 passes). PASS: out/lab/20261113-031719 (lifecycle_reset with
SPORE_STAGES=cell,creature, no monitor violation).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
