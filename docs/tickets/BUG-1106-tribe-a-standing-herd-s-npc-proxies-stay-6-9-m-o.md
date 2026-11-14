---
id: BUG-1106
title: Tribe: a standing herd's NPC proxies stay 6-9 m off the host's NPCs on one guest at the same spot of the campaign planet (monitor proxies gate)
status: not-reproduced
kind: desync
stage: Tribe
reported: 2026-11-14
source: rel/0.7.14 final-check verification lab 20261114-050940 campaign lane 2
role: guest
versions: 0.7.11+
fixed_in:
branch: fix/w49-campgate
commits: da38ba0c
tests: campaign (the full one, lane 2), tribe_proxy_held (the audit line's new fields)
signature: guest trace `proxy_stuck=6` on 7 lines in a row; audit line `proxy N: walk err 5.7-9.8 m, held 0, place lost 0, teleports 11..301 (about 5 a second), walked 0, nearest proxy M at 0.0-1.7 m ... game speed 72-124 m/s ... nest herd 1, host's speed 0.0`; the proxy back at the same two points after every teleport
docs: docs/known-issues.md#issue-tribe-proxy-spot-cadian
related: BUG-0169, BUG-0440, BUG-0503, BUG-0332
---

## Report
rel/0.7.14 final-check verification run out/lab/20261114-050940 (rel/0.7.14 worktree), test `campaign` on lane 2
(5393 s): `lab: gate monitor: 1 on extra2 (proxies): proxies: extra2: 6 NPC proxies over 3 m off the host's NPC
(Tribe) on 2 lines from s 3340`. The first final check (out/lab/20261114-005006) did not show it.

What the logs show (campaign.extra2.game.log, the world `Cadian.spo` 9652F8AD, the end of Tribe):
* s 3290-3331: the host votes evolve, goes into its Tribe editor (host_away=1 on every guest: the guests drop all
  proxies, `npc.cpp` tick_frame), comes back at s 3330.97 (`Alice (host) is back`). The proxies are made again.
* s 3333.6: proxy 489 (species 067257D0, in a nest herd of the guest's game) 8.0 m off its host NPC for 2.1 s; the
  host NPC's target jumped 96.5 m between two audits 2.1 s before (the first states after the return).
* s 3340-3400: `proxy_stuck=6` on all 7 trace lines until the next evolve vote (s 3408) ends the stage. The host's
  NPCs stand (speed 0.0, 0.2 m audit steps). The driver's stuck teleport fires about 5 times a second (teleports
  11 -> 45 -> 93 -> 144 -> 194 -> 247 -> 301), yet the proxy is found 5.7-9.8 m off at one of the same two points
  ((-41.6 -482.7 -169.4) and (-40.2 -483.1 -169.1), 1.5 m apart) every time; `game speed` 72-124 m/s (a 6-9 m move in
  one ~60 ms frame: put back after each teleport); the other proxies of the herd on top of it (nearest 0.0-1.7 m),
  `held 0`, `place lost 0`, `walked 0`, "driven" (not in the air, not placed on a path), no raised ground at the
  proxy or at its target.
* The other two guests (guest, extra1) had no stuck proxy at any time.

## Root cause
Not found (no runtime probe possible after the fact; see Verification).
* Observed: not a 0.7.14 regression. The same signature at the same spot of the same campaign planet in rel/0.7.11:
  out/lab/20261108-114435 (rel/0.7.11 worktree) campaign.extra2.game.log s 1584: proxy 195 (another species,
  0654E44E) 9.5 m off at (-42.8 -479.4 -175.4) (7 m from 0.7.14's spot), teleports 151, moved 0.0 m, host's speed 0.0,
  no editor return before it. There it was one trace line, so the gate (2 lines in a row) did not fire. Similar
  single lines (other spots): rel/0.7.12 20261111-044257 campaign extra1, fix/w46-attack-marker 20261108-063947,
  fix/w46-tribe-leader 20261107-163249, fix/w49-camp 20261113-133534.
* Observed: none of the 0.7.14 Tribe proxy switches is on this path: npc.proxy_hold acts only on walk_place's held
  placings (`held 0`, and its entries expire after 0.1 s); npc.air_catch / game.air_culled only on proxies in the air
  (this one is "driven"); npc.frozen_reused / npc.frozen_pooled_rts are host-side. The 0.7.11 case predates all of
  them. Not the BUG-1104 under-ground member (proxy_under=0, Tribe members' agl 0.00).
* Observed: the stuck spot is about 20 m from where the host's camera stood in early Tribe ((-51.3 -467.5 -180.2) at
  s 670, (-60.4 -465.0 -183.8) at s 1400 of the 0.7.14 run): the host's village area.
* Inferred (not checked): the host's standing NPCs are at a place this guest's game does not let a creature stand on
  (a building or object of this game only near the village; BUG-0332 found the guests' village ground differs from
  the host's), so every teleport onto it is pushed out to the same exit points by the next frame. The BUG-0169
  candidate (`drive::step`'s stall teleport) already teleports; it cannot win against that.

## Fix
None yet. Diagnostics (da38ba0c): the proxy audit line (npc.cpp monitor_tick) now also prints `objects within 3 m
of its target: <noun@m ...>` (what stands at the host NPC's spot in this game) and `air hold <none | d m from it>`
(game::air_held, a hold that puts the body back each frame), so the next occurrence names the mover.

## Verification
no-fail-run: not reproduced in a focused test. The spot exists only in the full campaign's own Tribe world (Cadian,
grown from the `cell` fixture through Creature; no Tribe fixture of it), after about 55 min of campaign; the
`tribe` fixture is another planet. The diagnostics' line format checked on lane 2: out/lab/20261114-065108
(tribe_proxy_held with npc.proxy_hold off, its planted case: `objects within 3 m of its target: none; air hold
none`). make test passed.

## History
- 2026-11-14: opened.
- 2026-11-14 (fix/w49-campgate): triaged: a real guest-side desync, pre-existing since at least 0.7.11 at the same spot, not a 0.7.14 regression and not one of its switches; not reproduced outside the campaign; audit diagnostics added (da38ba0c); known issue.
