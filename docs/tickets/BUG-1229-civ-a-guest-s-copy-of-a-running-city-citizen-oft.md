---
id: BUG-1229
title: Civ: a guest's copy of a running city citizen often moves in its standing animation (0x02481DE5) instead of the run (0x02481DB0)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-17
source: lab (fix/w51-trail, BUG-1227 measurement)
role: guest
versions: rel/0.7.15
fixed_in: 0.7.15
branch: fix/w51-citanim
commits: 4bebf0b9, ea51c4c0
tests: civ_citizen_trail (coop_civ_citizen_trail.py: `a moving copy plays the host's run`, `a standing copy is not in the run`; the key frames run1 / run2)
signature: guest copies (moving, animation): [((True, '02481DE5'), N), ...]: a copy moves while it plays the standing animation
docs: docs/re/civ.md#civ-citizen-run, docs/re/civ.md#civ-citizens-trail, docs/archive/known-issues-2026.md#issue-civ-citizens-slide, docs/06-testing.md#testing-civ-citizen-trail
switch: civ.citizen_run
related: BUG-1227, BUG-0476, BUG-1230, BUG-1231
---

## Report
civ_citizen_trail (BUG-1227) records every Civ city citizen's current animation on both games (`coop.bodyrec start
kinds za`, rows a<id>, game::drawn_state). On the host a citizen that moves (over 2 m/s, they run at 8 m/s) plays
0x02481DB0 (4755 of 4755 moving samples, lab 20261117-141344), a standing one 0x02481DE5 or an action. The guest's copy
of a running host citizen plays 0x02481DB0 too in only 8-28 % of the samples with civ.citizen_lead off (labs
20261117-141104, -141349, -141710) and 26-98 % with it, by session (-141344 26 %, -141059 91 %, -141705 98 %, -141917 76 %); the rest of the time it moves while it plays
0x02481DE5, the standing animation (3321-4550 such moving samples in a 60 s off run). The key frames run1 / run2 of those runs
(the same camera on both games at a running host citizen) show the host's citizen in a run pose and the guest's copy
crouched as if it stood.

## Root cause
Not analysed. Observed: the old path (MoveTo alone) has it more than the new one (the BUG-1227 glide is not the
cause: with it more copies run). Inferred: the copy's MoveTo walk is not the game's run of a citizen (the host's citizens
run by their own behaviour, which plays 0x02481DB0; game::gait_state's gait word is 0 for every citizen, so the walk gait
is not read that way), or the copy's own AI replaces the walk. Next: which call starts a citizen's run (the behaviour
that plays 0x02481DB0), give it to the copy instead of MoveTo, or play the host's animation on the copy.

## Root cause (fix/w51-citanim)
Observed (civ_citizen_trail's a<id> rows per copy, 4 recordings of 15 copies: fix/w51-trail lanes 4/6 141254, 141900,
fix/w51-citanim 20261117-144832, -144837): a guest's bound copy keeps ONE animation for the whole 60 s while the mod walks
it, every copy either 100 % 0x02481DB0 (the run) or 100 % 0x02481DE5 (the idle), moving or not. A copy bound in the idle
slides in its standing pose whenever it runs; one bound in the run runs in place while it stands (guest 37-59 % of the
standing samples in the run, host 5-13 %). The session dependence is how many copies happened to be in the run at the
bind. Not the cause: the MoveTo goal (10-12 m ahead), the per-frame place (the same with civ.citizen_lead off), the speed
(the copies run at the host's 8 m/s).
Observed (static, Ghidra): a citizen's run is played only by its behaviour tree, ~30 sites in node enters / ticks / exits
(0xD90E30..0xD97E98, e.g. the exits of nodes 0x05DE40D5, 0x048F79DA, 0x04C0492C play it) and 0xD57F30, each
PlayAnimation(0x2481DB0, -1, 0); the locomotion (MoveTo) plays nothing, and the idle refresh would choose 0x02481DE5 for a
citizen at any speed (no walk gait). Inferred, not known: why the guest's own behaviour plays nothing on a bound copy for
a minute (the copies of standing host citizens never gesture either: BUG-1231).

## Fix
`civ.citizen_run` (worldobj.cpp `citizen_run`, called from the citizen kind's per-frame `drive` on a guest): while the
host's citizen or the copy itself moves (on from 1.5 m/s, off under 0.3 m/s; the copy's own speed from its places,
smoothed over 0.15 s), the copy plays the citizens' run (`game::play_animation(c, 0x02481DB0, -1, false)`, as the nodes)
when another animation is on it; a standing copy in the run gets the game's forced idle refresh; at most every 0.2 s per
copy (about 200 plays and 170 refreshes a minute for 15 copies). `coop.fix civ.citizen_run off`: the copy's animation is
left alone (0.7.14). Wire bytes unchanged. civ_citizen_trail judges both halves: moving copies in the host's run at least
90 % and at most 8 points under the host, standing copies in the run at most 10 points over the host.
A first version (4bebf0b9: by the host citizen's speed only, a run ended only where it had been played) left the copies
that overran a stop (BUG-1230) sliding: moving 94-95 %.

## Verification
* FAIL (`SPORE_FIX_OFF=civ.citizen_run`, final code ea51c4c0): 20261117-150324:civ_citizen_trail (moving 94 %, standing in
  the run 23 % against the host's 7 %); 20261117-145434 (4bebf0b9: moving 90 %, standing 59 % / host 12 %),
  20261117-144832 (91 %, 45 % / 6 %). The switch goes off only after the join, so in these runs ~15 copies got the run
  in the first seconds: the old code is worse. The final test's checks over the recordings of fix/w51-trail (no
  civ.citizen_run, json of lanes 4 and 6, 141249 .. 142112): every one of 7 fails (moving 17-94 %, standing 18-50 %).
* PASS (ea51c4c0's code): 20261117-145821:civ_citizen_trail (moving 98 % / host 98 %, standing 13 % / host 7 %, p90 1.66 m),
  20261117-145826:civ_citizen_trail (97 % / 100 %, 14 % / 10 %, p90 1.59 m). 4bebf0b9: 20261117-145439 (94 %, 9 %).
* civ_citizens PASS 20261117-150329, 20261117-150553; play_civ --quick PASS 20261117-150548; make test passed.
* Key frames 20261117-145826/frames/civ_citizen_trail (run1, run2): the same two citizens in a run pose on both screens.

## History
- 2026-11-17: opened.
- 2026-11-17: fixed on fix/w51-citanim (civ.citizen_run; civ_citizen_trail judges the animations); BUG-1230, BUG-1231 opened.
- 2026-11-18: released in 0.7.15.
