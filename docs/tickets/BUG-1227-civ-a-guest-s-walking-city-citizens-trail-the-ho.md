---
id: BUG-1227
title: Civ: a guest's walking city citizens trail the host's by 3-6 m; neighbours cross and civ_citizens' size check sees another size at a place (sizes per bind are right)
status: open
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-citizens, BUG-1221 diagnostics)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in:
branch:
commits:
tests: civ_citizens, civ_citizens_reload (coop_civ_citizens.py: the `size pair` and `standing unambiguous pairs` lines)
signature: FAIL the guest's citizens have the host's size: N of M unambiguous pairs differ (11-14 %)
docs: docs/re/civ.md#civ-citizens-reload, docs/known-issues.md#issue-civ-citizens-trail
related: BUG-1221, BUG-0476, BUG-0475, BUG-1210
---

## Report
civ_citizens fails now and then on its size check (`2 of 18 unambiguous pairs differ (11%)`, fix/w51-t4 20261116-094048
#5 / #6, 1 of 12 runs without any relocation): a host citizen and the guest citizen standing 0.3-2.6 m from it have
sizes such as 1.38 / 1.58.

## Root cause
Observed (fix/w51-citizens, the test's new miss lines, lab 20261117-120456, -121708, -123109): every such pair is two
walking host citizens whose guest copies trail them by 3-6 m and cross: `size pair host 12 1.23 / guest 6 1.45 (1.3 m):
the guest's is bound to host 16 5.4 m away, size 1.45; the host's copy 2 5.7 m away, size 1.23` and the mirror pair
16 / 2 in the same round. Each copy has the size of the host citizen it is bound to (`fields civ_citizen ... differ=0`,
and 0 of 172 standing unambiguous pairs, both citizens moved under 0.5 m in the second before, had another size). So the
sizes are right; the places of walking copies lag: citizen_follow gives a MoveTo 1.5 s ahead of the host's last
snapshot place, again only when that goal moved 2 m or after 1.5 s, and puts a copy only beyond 12 m.
Inferred: the share of such pairs grows with the crowd's walking (right after a load, `pair distances: median 4-5 m`).

## Fix
None yet. Tried (fix/w51-citizens, not kept): after each MoveTo of a walking copy SetDesiredSpeed(host speed + up to
3 m/s while behind) (switch civ.citizen_catchup): lab 20261117-125759, 4 runs: the runs' median pair distance 0.6-1.9 m
against 0.5-2.6 m without (11 runs), crossings still with copies 5-6 m off (`the host's copy 16 5.5 m away`, two rounds
6 s apart, so not a speed lag alone: a copy that stays 5 m off for 6 s is held by something else, the game's own idle
walk of the citizen is the suspect). Options: a shorter walk lag for Civ citizens (a goal given every snapshot when it moved 1 m, a put beyond 6 m),
or the size check on pairs that are each other's copy by more than one round. The test prints the cause of every size
pair (what the guest's citizen is bound to, how far, its size).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-17: opened.
