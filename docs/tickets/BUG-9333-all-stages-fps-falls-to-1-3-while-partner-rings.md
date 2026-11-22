---
id: BUG-9333
title: All stages: FPS falls to 1-3 while partner rings are drawn (each ring point asked SporeKit for the camera, 4 VirtualQuery each)
status: fixed
kind: bug
stage: All
reported: 2026-11-21
source: owner play test 2026-11-21 (Live 68-dev, Tribe)
role: host
versions: 0.7.14.1-lineages68-dev
fixed_in: unreleased
branch: fix/l47-ring-fps
commits:
tests: tools/qa_win (manual, 2026-11-21): the owner's Tribe world copied into a test profile, host and guest on TITAN, kit.fix viewcache 0/1
signature: perf: hitch 290-330 ms every frame in Tribe while a partner is connected; prof: frames over 80 ms filled by the mod's own work 0
docs: docs/re/graphics.md#graphics-projection-cost
related: BUG-9321, BUG-9302, BUG-0791, BUG-0948
---

## Report
Owner (live 68-dev, hosting, Tribe): "our FPS went down to 3 when another player connected". Their report: 102 fps in
the Galaxy menu, then in Tribe with the partner connected 29, 12 and 17 fps a minute (p50 9 ms, p95 305-322 ms: about
every second frame took 300 ms); "frames over 80 ms ... filled by the mod's own work 0", so no tick was the cost. The
owner remembers it ended when the partner left (the log's last minute line mixes both, it cannot tell).

## Root cause
Observed (TITAN QA, the owner's world in a test profile, host + guest): with the partner rings on, 1.4-1.7 fps at the
QA cap of 10 (about 600 ms a frame); `coop.fix tribe.partner_ring off` + `all.partner_rings off` gave 9.8 fps at once,
on again 0.9 fps. The rings (BUG-9321, drawn at the game's depth clear) project every vertex with SporeKit's
world_to_screen: about 1000 points a soft ring (48 a circle, 2 halo and 7 glow bands, each vertex on its own for the
depth test). world_to_screen outside the overlay pass is world_to_screen_game -> read_view: a symbol lookup by name
and 4 skg::readable (one VirtualQuery each, a system call) for the mode manager, its viewer and the camera. 6-9 partner
villagers in view: about 35 000 system calls a frame. Inferred: on the owner's PC that is the 300 ms frame (no
other code path differs between the slow and the fast runs). Rings exist only while a partner is connected (its
villagers are the tracked members), which is why it began with the connection.

## Fix
SporeKit read_view (sporekit/gamehooks.cpp) keeps the pointers it checked (the manager, the viewer, the camera) and
trusts them while the game hands out the same ones on the same thread; they are checked again every frame (dropped at
the end of Present, at each game loop pass and at a symbol reload) and whenever one differs. The matrix and the
viewport are still read at every call, so every projection is the same as before. `kit.fix viewcache 0`: every call
checks everything (68-dev). `kit.cam` shows `viewcache fix= checked= cached=`.

## Verification
Same game, same world, 6-9 rings drawn, QA cap 10 fps:
FAIL (`kit.fix viewcache 0`): 1.7 and 1.6 fps a minute (present interval p50 576-588 ms), 99-102 hitches a minute.
PASS (`kit.fix viewcache 1`): 9.8 and 9.8 fps (p50 100.8 ms, p95 106-107 ms), 1 hitch a minute; kit.cam: 10.5 million
projections from the kept pointers, about one full check a frame. The rings still sit under the partner's villagers
and a bush in front hides part of them (screenshot, 800x600).
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL and PASS runs above are the switch off and on.

## History
- 2026-11-21: opened.
- 2026-11-21: reproduced on TITAN with the owner's world (test profile copy), rings proven as the cost (switches off/on), fixed in SporeKit read_view.
