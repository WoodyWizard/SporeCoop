---
id: BUG-0281
title: creature: an NPC proxy right next to a guest's avatar stays ~4 m from where the host has it
status: fixed
kind: desync
stage: creature
reported: 2026-10-29
source: fix/w7-sync BUG-0275 measurement
role: 
versions: 
fixed_in: 0.7.3.2
branch: fix/w14-creature
commits: aaa98ac
tests: creature_npc_dash (coop_tribe_npc_dash.py with DASH_FIXTURE=creature)
signature:
docs: docs/re/creature.md#creature-kinematic-proxies
related: BUG-0305, BUG-0169, BUG-0312
---

## Report
Found while measuring BUG-0275 (4 players, creature fixture, play_creature bots on every machine,
lane 6, 2026-10-29). In a few checks one NPC proxy sat 3.8-4.3 m from the host's NPC on every guest
at once while the host's NPC stood still (speed 0); it was the NPC 3-5 m from the guests' own
avatars (host NPC #1, species 065CBE70, 7 m from the host's avatar). The guest's `coop.npc list`
showed it: `err=4.33` / `4.32` / `3.76` (the proxy's distance from its own interpolation target
`want`, age 0.02-0.11 s), so the proxy does not reach the pose it is driven to. Likely the guest's
own avatar (or the puppets next to it) pushes the proxy away, the host's NPC is not pushed the same
way (inferred, not verified). Players see a standing NPC some metres off next to them.
Reproduce: SPORE_LANE=N, 4 players in the creature fixture (coop_multi.start_extra), play_creature
bots, `coop.npc list` on a guest: lines with `err=` above 2 m while the host's NPC stands.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not found; not reproduced (2026-10-30, fix/w9-creature, lane 9):
* 2 players, the guest's avatar teleported 0.3 / 1.5 / 3 m beside three host NPCs that stood still
  (`coop.npc gopos`), 6 s each, `coop.npc list` and both censuses every 0.5 s: the proxy stayed
  0.1-0.6 m from its target and from the host's NPC; the guest's avatar did not push it 4 m. One NPC
  that walked off meanwhile trailed 3.5-4.2 m for about 2 s (host and guest apart by the same, a
  moving NPC) and caught up.
* 4 players, play_creature bots (`coop_sync_multi.py --fixture creature --rounds 45`): no standing
  NPC 4 m off on every guest at once was singled out; the worst NPC errors of rounds 21-32 were
  2-19 m on 1-3 guests (n22, n25, n33, n35, n36, moving NPCs of a fight near the players; p90
  median 0.64-1.11 m per guest). The jsonl has no speed, so "standing" could not be checked there.

Possibly the same mechanism as BUG-0305 (2026-10-30, fix/w12-tribe-npc; inferred, not reproduced
in the Creature stage): there, a proxy pushed off its path by the proxy beside it stayed 2-4 m from
its target on every guest while the animal ran (the proxy cannot run faster than the animal). The
`npc.trail` fix of BUG-0305 is in the same driving (npc.cpp, drive::step) and also acts in the
Creature stage: a proxy 1.5 m off for 0.5 s is put on its target and held 1 s. A proxy next to a
standing host NPC 4 m off (this report) should now be put in place within 0.5 s (before: the stuck
rule after 1.5 s, the stall rule after 3 s); not checked in the game.

Checked in the Creature stage on fix/w13-creature (lane 5, 2026-10-30, docs/re/creature.md#creature-proxy-guest-side):
* Standing: the guest's avatar 0.5 / 1.0 / 1.5 / 2.5 m beside three standing host NPCs, 6 s each,
  the guest at 15 fps (`frame.limit 66`, as a loaded 4-player guest): proxy errors 0.0-0.5 m. The
  standing case of the report did not show (also not with the guest's avatar pushing).
* Running: two nest NPCs (065CBE70) run side by side on the host (`coop.npc dash`, 4 m/s, 12 m
  zigzag), the guest at 15 fps 1.5 m beside the path's end (`DASH_FIXTURE=creature
  coop_tribe_npc_dash.py`): p90 6.2 m off the host's path with `npc.trail` off, 3.1-3.4 m with it
  (3 runs); Tribe 0.7-0.95 m. `npc.trail` (BUG-0305) helps in Creature but does not remove the lag.
  These proxies hardly walk by themselves (`own=` 0 for most samples, 1-3.3 m/s at best while the
  NPC runs at 4 m/s); a nudge scaled by the frame time (the same pull per second at 15 fps as at 60)
  was tried and gave 3.4 m: dropped (commit 3a62e2b reverts 05269fb).
* ~ superseded (2026-10-30, fix/w14-creature): "not found why". CORRECTION (2026-10-30,
  fix/w14-creature): found, see below.

Found on fix/w14-creature (lane 5, 2026-10-30, `coop.npc pwalk <id> x y z <m/s> <s> [mode]`, a test
command that walks one guest proxy with the game's locomotion and keeps the sync off it):
* One `MoveTo` 12 m ahead: the 065CBE70 proxy ran at its 4 m/s at once (others 2.5-4 m/s): the
  locomotion of a proxy works.
* The same with the `MoveTo` given again every 0.1 s (what drive::step does while the target moves
  on): the proxy stood (speed 0), went back, crept at 1-2 m/s. Each new MoveTo restarts the walk.
* Writing the position to itself or the orientation every frame did not slow it.
* While walking, our pulls towards the path (set_position) were undone by the locomotion: `nudge=`
  kept 14-76 of 519-624.
* Giving the walk again less often (only when the target moved 1.5 m or after 0.5 s) or 2-4 s
  ahead did not help: p90 3.3-3.9 m (the zigzag turns every 3 m).
So the Creature proxy of a running NPC moved mostly by our teleports (npc.trail) and pulls, and
trailed. (Tribe passes because its animals are driven the same way but its test path fits their
locomotion; not looked into.)

## Fix
`npc.kinematic` (npc.cpp, Creature stage only): while the host's NPC moves (its speed > 0.3 m/s) and
the proxy is within 8 m of the sampled host position, the proxy is teleported onto the interpolated
host position with the host's orientation every frame and gets the host's velocity (the game
animates the run from it: stills show running poses). When the NPC stands the old driving (walk,
nudge, stuck/stall/trail) takes over; the velocity is zeroed once. `coop.fix npc.kinematic off`: the
driving always (0.7.3.1). `set_position` instead of the teleport left p90 1.5-2.0 m (the proxies
pushed each other); the teleport puts it exactly there.

## Verification
`DASH_FIXTURE=creature coop_tribe_npc_dash.py` (suite creature_npc_dash): two nest NPCs run side by
side at 4 m/s, the guest at 15 fps beside the path's end; 90 % of the guest's samples within 1.2 m
of the host's path.
* FAIL with `SPORE_FIX_OFF=npc.kinematic`: out/lab/20261030-161510 (p90 1.84 m on main d003127 with
  BUG-0175's per-second nudge; 3.3-3.5 m before it).
* PASS: out/lab/20261030-161659 (2 runs: p90 0.34 and 0.31 m, median 0.05 m).
Standing NPCs (the report's case) were not reproduced (see above); the standing driving is unchanged.

## History
- 2026-10-29: opened.
- 2026-10-30: not reproduced on fix/w9-creature (2-player probe beside standing NPCs, one 4-player measurement).
- 2026-10-30: fix/w11-finale: the same pattern in the Tribe stage with 4 players (one animal ~4 m off on every guest at once): BUG-0305.
- 2026-10-30: BUG-0305 (Tribe) found proxies of a running herd pushed off their path; its `npc.trail` fix also acts here; not reproduced in Creature, stays open (fix/w12-tribe-npc).
- 2026-10-30: fix/w13-creature: Creature check of npc.trail: standing case not reproduced; running NPCs p90 6.2 m -> 3.1-3.4 m with npc.trail at 15 fps; stays open.
- 2026-10-30: fix/w14-creature: cause found (a MoveTo again every 0.1 s restarts the walk; pulls undone by the locomotion); fixed by npc.kinematic.
- 2026-11-01: released in 0.7.3.2.
