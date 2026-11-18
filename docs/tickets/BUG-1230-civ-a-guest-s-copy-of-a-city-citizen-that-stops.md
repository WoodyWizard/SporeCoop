---
id: BUG-1230
title: Civ: a guest's copy of a city citizen that stops runs on up to 10-12 m past the host's citizen and back
status: fixed
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-citanim, BUG-1229 measurement)
role: guest
versions: 0.7.14.1, rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-overrun
commits: aabe3e90
tests: civ_citizen_trail (coop_civ_citizen_trail.py: `a standing citizen's copy is within 1.5 m of the host's citizen`), civ_citizens
signature: a copy of a standing host citizen 3-12 m off it, moving at up to 8 m/s away and back (every 2-3 s, not only at stops)
docs: docs/re/civ.md#civ-citizen-hold, docs/re/civ.md#civ-citizen-run, docs/06-testing.md#testing-civ-citizen-hold
related: BUG-1227, BUG-1229, BUG-0476
---

## Report
Found while measuring BUG-1229 (civ_citizen_trail's recordings, `coop.bodyrec kinds z`: both games' citizens as drawn
at the same moment). While a host city citizen stands (under 0.5 m/s), its copy on the guest is over 3 m away from it in
5-24 % of the samples (max 9.9-12.2 m) in every one of 26 recordings, with civ.citizen_lead on and off (labs of
fix/w51-trail 20261117-133529 .. -141917, fix/w51-citanim 20261117-144832 .. -145826). Per copy (lab 20261117-144837,
z2): the host's citizen stops, the copy runs on at 8 m/s to 8-10 m past it, then back (1-2 s), at most every stop.
civ_citizen_trail's distance checks only take the samples where the host's citizen moves, so this is not judged.
Reproduce: the analysis in the ticket's history (samples where the host's citizen stands, the copy's distance).

## Root cause
Observed (civ_citizen_trail's z/y/a rows: 6 recordings of fix/w51-trail and this branch's labs; docs/re/civ.md#civ-citizen-hold).
The inference above (a walk goal 12 m ahead left at the stop) is wrong: the far samples come mostly 3 s to more than 10 s
after the host's citizen stopped (z3 of 20261117-141249: 12 within 1 s of the stop, 1214 later), and the walk goal y<id>
is the host citizen's place all the time. While the host's citizen stands, the guest's own behaviour still walks the
bound copy: every 2-3 s the copy (standing within 1 m) runs off at 8 m/s, and citizen_follow (every 0.25 s) walks it
back only when it is over kCitizenWalk (1.5 m) off and its last MoveTo to the same point is 1.5 s old, so 8-12 m out
and back. A StopMovement or an at-once MoveTo back does not end it: the behaviour walks it again within a second and
plays the run each time (labs 20261117-152554, -153309: 2-3 m out and back every 1-2 s, standing copies in the run 23-36 %).

## Fix
`civ.citizen_hold` (mods/coop/worldobj.cpp `citizen_hold`, from the citizen kind's per-frame `drive` on a guest): while
the host's citizen stands (under 0.3 m/s) the copy is placed every frame (no teleport mark, at its own distance from the
planet's centre) a part of the way to it (time constant 0.06 s, at most 10 m/s; nothing within 0.15 m); a copy over
1.5 m off that moves away gets a MoveTo back at once (at most every 0.3 s); within 1.5 m citizen_run (BUG-1229) takes it
as standing, so its own walk against the place does not keep it in the run (one idle refresh ends it).
`coop.fix civ.citizen_hold off`: only citizen_follow's walk (as before). civ_citizen_trail now judges the standing
samples: the copy within 1.5 m of its host citizen in 90 % of them (FAIL 3.41-4.50 m, PASS 0.54-0.55 m).

## Verification
* FAIL (`SPORE_FIX_OFF=civ.citizen_hold`, final code aabe3e90): 20261117-153909:civ_citizen_trail (lane 4: standing
  p50 0.47 p90 4.50 m, over 3 m 13.5 %, max 11.3 m; also BUG-1229's `a standing copy is not in the run` 32 % against
  the host's 10 %). Earlier code, same switch off: 20261117-152003 (p90 3.41 m, over 3 m 11.0 %, max 11.0 m).
* PASS (aabe3e90): 20261117-153627:civ_citizen_trail (lane 4: standing p90 0.54 m, max 2.8 m, over 3 m 0.0 %; standing
  copies in the run 1 %, host 5 %), 20261117-153630:civ_citizen_trail (lane 6: p90 0.55 m, max 2.7 m; 0 %, host 4 %);
  the moving checks unchanged (p90 1.56 / 1.58 m, crossed 2.1 / 3.0 %, run 98 %).
* civ_citizens: 20261117-153912, 20261117-154126 PASS (0 of 152 without a partner, 0 sizes differ).
* rel/0.7.15 without the fix failed BUG-1229's standing-run check in 2 of 2 runs (20261117-152008 in the rel/0.7.15
  tree: 21 % against 5 %; 20261117-152224 with an early variant of this fix: 20 % against 3 %): the copies that ran off
  played the run. With the hold it passes.

## History
- 2026-11-17: opened.
- 2026-11-17: cause observed (the guest's own walks of a standing copy, not the stop); fixed on fix/w51-overrun (civ.citizen_hold), civ_citizen_trail judges standing copies.
- 2026-11-18: released in 0.7.15.
