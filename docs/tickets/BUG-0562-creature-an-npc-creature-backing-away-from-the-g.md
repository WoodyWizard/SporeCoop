---
id: BUG-0562
title: Creature: an NPC creature backing away from the guest gets stuck inside its nest (video)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: unreleased
branch: fix/w45-last
commits: ef1058d0, 2c012d76, 20236abd, 694c8cf3
tests: npc_nest_backoff (coop_npc_nest_backoff.py), npc_nest_backoff_near
signature:
docs: docs/re/creature.md#creature-nest-mound-proxy
related:
---

## Report
The guest posted a video: a creature got stuck into the nest after backing away from it.
Reports: SporeCoop-report-2026-11-04 (Discord video) (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Discord video only, no log line. Inferred: the host's game walks the NPC back (its AI), the guest's proxy follows
  the host's path through the nest model; not analysed.

* fix/w45-creature-reports (P3 of 0.7.9): no log or frame of the video in the reports (only a Discord video, not
  available to the agent). The 0.7.5 guest log of that session has 22 lines "npc: proxy N ... 1.4 m under this game's
  ground ... the host's NPC 0.0 m from it (driven)": proxies drawn inside the guest's ground, the class fixed in 0.7.8
  (`drive::at_sender_agl` / npc.proxy_ground, BUG-0425 / V1). Inferred: the NPC "inside its nest" is a proxy drawn in the
  nest mound on the guest (the mound is collision, not terrain: BUG-0434 / BUG-0435 for puppets), or a proxy walked
  against the mound by the guest's own locomotion while the host's NPC backed off through it (the backing away is the
  BT node 0x02D852E0, docs/re/creature.md#creature-roar-relay). Regression check in the lab: npc_walk (the host's NPCs
  wander, walk, run and flee around the guest at the fixture's home nest; the guest's proxies sampled every frame).

* fix/w45-last (L5 of 0.7.9), observed in the game (coop_npc_nest_backoff.py, lanes 5; docs/re/creature.md#creature-nest-mound-proxy):
  with the host's player 120 m from the fixture's nest and the guest at it, the host's game had the nest's animal that
  rests nearest the mound's centre 0.43-0.97 m **inside** the mound in every cold run (9 of 9); with the host at the
  nest none (npc_nest_backoff_near, lab 20261106-125020). The guest's proxy was drawn at the host's place, inside the
  guest's mound, 0.47-0.76 m deep, on the guest's screen for 11-13 s of 24 s (the guest's camera is at the nest, so its
  mound is solid and the game does not lift a creature put that deep). The roar's back-off (node 0x02D852E0) takes it
  out of the mound within 3-5 s; it walks back to rest at the nest later. Inferred: the host's game simulates the
  creatures far from its own camera without the mound (as #creature-out-of-view-ground found for puppets); the video's
  "stuck into the nest after backing away" is that animal resting in the mound on the guest's screen. Not seen: the
  video itself.

## Fix
`npc.proxy_mound` (npc.cpp, BUG-0562): on the guest a proxy (not in the air, not dead) whose host target or drawn place
is 0.1-2.5 m under this game's surface (cPlanetModel_SurfacePoint: the mound), where that surface is over the plain
terrain, is put 5 cm under the surface; the game sets it onto it. `coop.fix npc.proxy_mound off`: the host's position
(0.7.8). Diagnostics: `coop.ground x y z` prints the point's height over the surface, `coop.npc` counts mound_lifts /
mound_targets.

## Verification
npc_nest_backoff: FAIL with `SPORE_FIX_OFF=npc.proxy_mound` out/lab/20261106-131754 (2 of 2: the animal 11.2 / 13.3 s inside the
mound on the guest's screen; also out/lab/20261106-130629, 2 of 2), PASS with the fix out/lab/20261106-132117 (3 of 3:
the host's animal 0.54-0.72 m in the mound, 0 s on the guest). Control npc_nest_backoff_near PASS out/lab/20261106-125020.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open, not analysed (video only).
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): not started (the time went to BUG-0501, BUG-0558, BUG-0496, BUG-0557).
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): not reached (the time went to BUG-0600 and BUG-0465).
- 2026-11-06 (fix/w45-creature-reports, P3 of 0.7.9): not reproduced (video only); likely the 0.7.8 height class or BUG-0434; npc_walk run as the regression check; left open.
- 2026-11-06 (fix/w45-last, L5 of 0.7.9): reproduced (the host's nest animal inside the mound when the host is far, drawn so on the guest's screen); fixed by npc.proxy_mound; test npc_nest_backoff.
