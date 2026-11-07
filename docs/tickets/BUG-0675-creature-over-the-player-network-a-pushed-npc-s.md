---
id: BUG-0675
title: Creature: over the player network a pushed NPC's copy stands while the host's NPC is thrown, then steps after it (body_trail_hits at 30 fps, SPORE_NET=player: over 2 m 0.88-0.96 s, max 6-7.4 m)
status: open
kind: desync
stage: Creature
reported: 2026-11-07
source: fix/w46-k3-bodies labs 20261107-103414, 20261107-112617-2
role: guest
versions: 0.7.9 driving (NPC proxies unchanged in 0.7.10)
fixed_in:
branch:
commits:
tests: body_trail_hits (SPORE_FPS=30 SPORE_NET=player)
signature:
docs:
related: BUG-0409, BUG-0425, BUG-0674
---

## Report
body_trail_hits at 30 fps over SPORE_NET=player (45 ms, +250 ms spikes, loss bursts) failed 2 of 4: out/lab/20261107-103414 #2 (n19 hit at 44.0 s with a push: over 2 m 0.96 s, max 6.01 m) and out/lab/20261107-112617-2 #2 (n7 at 44.1 s, push: 0.88 s, 7.43 m); the limits are 0.75 s and 6 m. On a clean network the same test passes (out/lab/20261107-103912 2 of 2, 20261107-101948 era runs). In both the guest's game never threw the proxy (guest lm 0 only, the host's NPC lm 3 for 104-165 frames).

How to reproduce: SPORE_FPS=30 SPORE_NET=player tools/lab.py run body_trail_hits --repeat 4 --retry 0.

## Root cause
Observed (coop.bodyrec rows of 103414 #2, n19 after 44.0 s): the copy stood for 3-6 guest frames at a time (step 0) while the host's NPC moved 0.4-1.2 m a frame, then stepped 1.9-2.1 m: 7.2 m off at 45.18 s. Inferred: the lead does not move a body on whose path between its two newest states is faster than 15 m/s (net/interp.h lead_velocity: taken as put somewhere), and a push throws the host's NPC at 13-31 m/s, so the copy waits for each state; over the player network the states of a throw come in a spike or a loss burst and the waits add up. Not BUG-0409's hold (the guest's game undoing the placing): no throw on the guest.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
- 2026-11-07: opened (fix/w46-k3-bodies, K3, from the bad-network attempt on BUG-0409); not fixed there.
