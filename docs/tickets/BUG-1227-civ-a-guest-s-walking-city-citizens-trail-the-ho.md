---
id: BUG-1227
title: Civ: a guest's walking city citizens trail the host's by 3-6 m; neighbours cross and civ_citizens' size check sees another size at a place (sizes per bind are right)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-citizens, BUG-1221 diagnostics)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-trail
commits: b9ebfb92, 937b638d, fa196259
tests: civ_citizen_trail (coop_civ_citizen_trail.py, new), civ_citizens, civ_citizens_reload (coop_civ_citizens.py: sizes by identity)
signature: FAIL the guest's citizens have the host's size: N of M unambiguous pairs differ (11-14 %)
docs: docs/re/civ.md#civ-citizens-trail, docs/re/civ.md#civ-citizens-reload, docs/06-testing.md#testing-civ-citizen-trail, docs/archive/known-issues-2026.md#issue-civ-citizens-trail
related: BUG-1221, BUG-0476, BUG-0475, BUG-1210, BUG-1229, BUG-0305
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

## Measurement (fix/w51-trail)
How a player sees it: both games record the citizens as drawn at the same moment (`coop.bodyrec start kinds z`, rows
z<host id> on both games, so each copy is compared with its own host citizen; civ_citizen_trail), next to the other
mirrored walkers of the same session. With the old driving (`SPORE_FIX_OFF=civ.citizen_lead`, labs 20261117-133529,
-133940, -135246, -140134, -140750, -141349): walking copies p50 4.7-5.2 m, p90 7.2-7.9 m off, lag p50 0.59-0.65 s
(p90 0.92-0.98 s), stretches over 2 m of 16-35 s; in 42-56 % of the moving samples the copy stood over 2 m off with a
neighbour's copy nearer the host citizen. In the same sessions NPC proxies p90 0.25-0.54 m (lag p50 0.007-0.023 s),
Civ vehicles p90 0.08-0.13 m (lag p50 0.003-0.007 s); Tribe members p90 0.64 m (docs/re/creature.md#creature-remote-lead).
So the citizens lagged 25-80 times more than the other walkers: not the relay's normal lag.

## Root cause (fix/w51-trail)
Observed: the host's citizens move in runs of a steady 8.0 m/s (not a walk). citizen_follow's MoveTo goal was right
(10-12 m ahead of the host's citizen, `walk goals from the host's citizen p50 10.4 m`), but the copy runs at the
citizen's own top speed, the host's, and keeps the 4-8 m it lost at each start (the start is known a snapshot later and
the velocity after two, 0.5-1 s): the copy's distance stayed the same for seconds (`zf` 7.3 m for 5 s). The class of the
Tribe animal proxies (BUG-0305): the game's locomotion does not run a creature faster than its top speed. The earlier
catch-up speed try (SetDesiredSpeed) could not go over it either.

## Fix
`civ.citizen_lead` (worldobj.cpp `citizen_drive`, the citizen kind's per-frame `drive` on the guest): while the host's
citizen moves, the copy (still walked by MoveTo) is placed without the teleport mark a part of the way to where the
host's citizen is now (last place + velocity x the snapshot's age, at most 1 s), closing the error with a 0.12 s time
constant, at most 4 m/s over the host's speed. The host sends citizens every 0.25 s (was 0.5 s) to guests of 0.7.15 on
(HELLO families; an older guest still gets every 0.5 s, its velocity needs snapshots over 0.3 s apart), and such a guest
takes the velocity from snapshots over 0.15 s apart. Off: MoveTo only, 0.5 s (0.7.14). Wire bytes unchanged.
Tries: the glide at 0.5 s snapshots p90 3.4 m (-133935); 0.125 s snapshots p90 1.7-1.9 m with a 0.25 s glide (not
kept, the gain small for twice the bytes). Left: sharp turns at 8 m/s (p90 2.7-3.5 m in the 0.6 s after a turn over
60 degrees).
civ_citizens' size check pairs by identity (the guest citizen bound to the host's, within 15 m), not the nearest pairs,
so crossing copies no longer count; the limits are unchanged (12 %, 10 % of the pairs).
On the way: BUG-1229 (open): a copy of a running citizen often moves in its standing animation, with and without the fix.

Earlier (fix/w51-citizens):
None yet. Tried (fix/w51-citizens, not kept): after each MoveTo of a walking copy SetDesiredSpeed(host speed + up to
3 m/s while behind) (switch civ.citizen_catchup): lab 20261117-125759, 4 runs: the runs' median pair distance 0.6-1.9 m
against 0.5-2.6 m without (11 runs), crossings still with copies 5-6 m off (`the host's copy 16 5.5 m away`, two rounds
6 s apart, so not a speed lag alone: a copy that stays 5 m off for 6 s is held by something else, the game's own idle
walk of the citizen is the suspect). Options: a shorter walk lag for Civ citizens (a goal given every snapshot when it moved 1 m, a put beyond 6 m),
or the size check on pairs that are each other's copy by more than one round. The test prints the cause of every size
pair (what the guest's citizen is bound to, how far, its size).

## Verification
* FAIL (`SPORE_FIX_OFF=civ.citizen_lead`, final code fa196259): 20261117-141710:civ_citizen_trail (`p50 4.30 p90 7.43 m,
  lag p50 0.55 s`, `over 2 m off with another copy nearer in 52.2 %`; NPC proxies p90 0.50 m, Civ vehicles 0.14 m in the
  same session). Earlier test versions, off: 20261117-133529, -133940, -135246, -140134, -140750, -141349 (p90 7.2-7.9 m).
* PASS (fa196259): 20261117-141705:civ_citizen_trail (`p50 0.28 p90 1.53 m, lag p50 0.04 s`, crossed 3.1 %; NPC proxies
  p90 0.75 m, vehicles 0.11 m), 20261117-141917:civ_citizen_trail (`p50 0.38 p90 1.53 m`, crossed 2.5 %); earlier
  versions -134347, -134352, -134936, -134941, -135241, -140745, -141059, -141344 (p90 1.38-2.00 m).
* civ_citizens --repeat 6 --until all: 6 of 6 PASS 20261117-135614 (sizes by identity: `0 of 152 pairs differ` in each)
  and 6 of 6 on the final code 20261117-143245; civ_citizens_reload PASS 20261117-141705 (`2 of 304 without a partner`, `0 of 304
  pairs differ`); civ_citizen_lod PASS 20261117-141705; play_civ --quick PASS 20261117-142640 (the first run
  20261117-141917 failed on the monitor gate `1 NPC proxies under the ground (Civilization)`, BUG-1216's class, not
  citizens).
* Key frames 20261117-141705/frames/civ_citizen_trail (run1, run2): the same citizens at the same places on both screens.

## History
- 2026-11-17: opened.
- 2026-11-17: fixed on fix/w51-trail (civ.citizen_lead, civ_citizen_trail, civ_citizens sizes by identity); BUG-1229 opened.
- 2026-11-18: released in 0.7.15.
