---
id: BUG-1360
title: Creature: at 30 fps a walked NPC proxy on a straight run plays the standing idle 02481DE5 for 1-6 s (npc_walk_novel gait 79-80%)
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: final-check-20261120-041304
role: 
versions: 0.7.15.5, unreleased
fixed_in:
branch: fix/w76-motion
commits:
tests: npc_walk_novel, npc_walk_novel_stale, npc_walk_novel_stale_gait_off (twin), npc_walk_novel_gait_off (twin), npc_walk_fps30, npc_walk_player, tests/npc_tests.cpp test_walk_gait
signature:
docs: docs/re/creature.md#creature-proxy-stale-walk-speed
related: BUG-0394, BUG-0436, BUG-1208, BUG-0361
switch: npc.walk_gait
---

## Report
Final check of 0.7.16 (lab 20261120-041304, lane 3) and the run before it on the same tree (20261119-214823, lane 2):
npc_walk_novel FAIL "the proxies walk where the guest sees them (walk gait in 80% / 79% >= 80%)"; the 0.7.16 run also
"no jump of a moving proxy (2)" (proxy #18 1.43 / 1.54 m at s 47.5, the flee phase's hit). History before: 0 of 7 failed.

## Root cause
Observed (the per-frame tracks out/lane3/track/npc_walk_creature_20261120-085656.json and
out/lane2/track/npc_walk_creature_20261120-032347.json, the test's own filters): the frames without a walk gait are
runs of 1-6 s in which a walked proxy (how=3, placed on the host's path every frame, rv 4.0 m/s: a straight flee or a
run of the walk phase) plays the plain standing idle 0x02481DE5 (gait 0, moving flag 1, speed state 2, grounded) while
it moves at the path's 4 m/s, trailing its target by a steady 0.82 m; it walks again (04866DAB) when the path turns or
slows. One or two proxies per run carry most of it: #21 283 of 600 on-screen frames (one run of 5.7 s, 51.3-57.0 s),
#20 310 of 974 in the other.
Observed (the same final check): at 60 fps npc_walk has 98 % (02481DE5 not among its top four), at 30 fps npc_walk_fps30
86 % and npc_walk_player 89 % with 02481DE5 the second most frequent, npc_walk_novel 80 %. Older runs of npc_walk_novel
(other branches, 30 fps) had the same idle frames at 83-84 % (passed) and 92-97 %. So it depends on the frame rate and
is not new in 0.7.16; the two 0.7.16 runs are at the low end of the spread.
Not known: what makes the game's creature switch to the idle while the mod places it on the path and its MoveTo goal is
12 m ahead (a goal reached / a speed the locomotion reads at 30 fps?). Not shown by the track (it has no MoveTo goal,
desired speed or locomotion state columns).

Observed (third run, second final check 20261120-133703 lane 3, track out/lane3/track/npc_walk_creature_20261120-204929.json
of the rel/0.7.16 tree): gait 78 % of 4402 frames; walked frames playing 02481DE5: 766 (#21 again, on screen 905 gait
454); its idle run 49.2-55+ s at rv 4.00 m/s, trailing 0.86-0.98 m, grounded, locomotion mode 0. The game's own speed
(gv, cLocomotiveObject velocity) in those walked idle frames p10/p50/p90 0.28/0.31/2.1 m/s against 0.16/1.26/3.98 in the
walked frames with the walk clip 04866DAB: the game's locomotion barely moves the proxy while the placing carries it.
Inferred: the run lasts ~4x the 1.5 s max_gap of walk_place's MoveTo, so a goal 12 m ahead is given again several times
during it (or the goal is not the cause); what the locomotion was asked is not in the track yet.
Found (2026-11-23, fix/w76-motion, the same track, idea from fix/agy-open-bugs fc420dcb): the game's own speed of #21
is 0.28-0.31 m/s in EVERY walked frame of the run (also those with the walk clip), not only in the idle runs: the mod
walks it at that speed. npc.cpp guest_tick: a walked proxy whose state reports no speed (here every state: the test's
`coop.npctrack novel 1`) goes on at its path's velocity clamped to px.walk_sp (BUG-0361's stop clamp: at a stop the
extrapolated target jumps back, a 37 m/s path). walk_sp is set only by states WITH a speed and never cleared: the slow
reported walk of #21's first seconds (0.31-0.36 m/s rows in the track) clamped all its later walks to ~0.3 m/s; the
game's locomotion at 0.3 m/s plays the standing idle, and catch_prop's placing ((0.3 + 3.3) m/s against a 4 m/s path)
keeps it 0.6-0.8 m behind, growing slowly (0.64 -> 0.78 m over 2.5 s in the track). Which proxies and how often
depends on what each one reported before (at 60 fps fewer frames are clamped walks: the 98 % of npc_walk). Mod bug,
observed in the track, the mechanism read in the code; docs/re/creature.md#creature-proxy-stale-walk-speed.

## Fix
2026-11-23 (fix/w76-motion), `coop.fix npc.walk_gait` (on; off = 0.7.16.1 exactly): the walk-velocity rule is the
portable npc_logic::walk_velocity (mods/coop/npc_logic.h, called from guest_tick): walk_sp belongs to one walk. A proxy
that is not walked and whose state reports no speed (it stands, or an unreported walk starts) forgets it, so an
unreported walk goes on at its path's velocity; a path jump over 15 m/s is no velocity there (drive::path_velocity's
rule: with walk_sp 0 more often, the old unclamped 37 m/s stop jump would otherwise pass). Within a walk with a
reported speed the stop clamp and the slower-path walk speed are unchanged. `coop.npctrack novel` also clears every
proxy's walk_sp (the test's toggle); `coop.npctrack walksp V` (tests) sets it on every proxy (the lab's stale case at
will). Not taken from fc420dcb: its nest-mound half (game::place for the residual mound lift, no on_own_ground on a
mound): no walked idle frame was on a mound, and an unmarked placing is undone by the game's update (BUG-0440), which
would bring back BUG-0562 (docs/re/creature.md#creature-proxy-stale-walk-speed). Unit test: tests/npc_tests.cpp
test_walk_gait (the lab's 0.31 m/s case, the BUG-0361 stop clamp, the 15 m/s jump, the slower-path walk; FAILs with
`--nofix`). Lab tests: npc_walk_novel_stale (every proxy's last walk seeded at 0.35 m/s after `novel 1`) and its twin
npc_walk_novel_stale_gait_off (deterministic FAIL); npc_walk_novel_gait_off (the plain case, a FAIL in some runs).
Earlier:  2026-11-20 (fix/w61-creature-c): `coop.npctrack dump` has the columns the night diagnostic needs (dsp: the
desired speed spatial +0x1E0, mmode: the movement request mode +0x310, gdist: m to its destination +0x2C8, the `?`
offsets of docs/re/creature.md), and coop_npc_walk.py prints them for the walked idle frames against the walked walk
frames ("BUG-1360 walked, idle 02481DE5 / walk gait"). No check changed. Next: a night diagnostic run of npc_walk_novel / npc_walk_fps30 with the track extended by the proxy's
MoveTo goal, its desired speed and the locomotion's own state for the frames where it plays 0x02481DE5 while placed,
then a fix in drive::walk_place with a switch and the 30 fps tests as its proof. The gate is not loosened.

## Verification
Open, waiting for the lab run (pending night run: npc_walk_novel npc_walk_novel_stale npc_walk_fps30 npc_walk_player
npc_walk_novel_stale_gait_off npc_walk_novel_gait_off):
`SPORE_DETECTOR=20 python3 tools/lab.py run npc_walk_novel npc_walk_novel_stale npc_walk_fps30 npc_walk_player --repeat 2
--lane N --retry 0 --no-build` must PASS (walk gait >= 80 %, the walked idle frames gone from the BUG-1360 lines);
`SPORE_DETECTOR=20 python3 tools/lab.py run npc_walk_novel_stale_gait_off --repeat 2 --lane M --retry 0 --no-build` must
FAIL on the walk gait; `npc_walk_novel_gait_off --repeat 4` FAILs in some runs (the old 2-3 of 9). Regression:
npc_walk, creature_npc_dash, npc_nest_backoff, puppet_mound_view.

## History
- 2026-11-20: opened from the 0.7.16 final check (fix/w60-misc): analysis of the two failing runs' tracks; frame-rate
  dependent, not a 0.7.16 regression by the history; open.
- 2026-11-20 (orchestrator): not a 0.7.16 regression (83-84% with the same idle on older branches); the cause needs the extended walk track from a diagnostic run. Planned for 0.7.17, does not block 0.7.16.
- 2026-11-20 (fix/w61-creature-c): third failing run (second final check 20261120-133703, 78 %, the same idle on #21);
  the game's own speed in the idle frames ~0.3 m/s; diagnostic columns added to npctrack for the night run
  (`SPORE_DETECTOR=20 python3 tools/lab.py run npc_walk_novel npc_walk_fps30 --lane N --retry 0 --no-build`). Still
  open; not a 0.7.16 regression (orchestrator: 0.7.17).
- 2026-11-23 (fix/w76-motion): cause found in the track (the stale walk_sp clamp), npc.walk_gait, unit test, lab test + twins; the mound half of fc420dcb not taken.
