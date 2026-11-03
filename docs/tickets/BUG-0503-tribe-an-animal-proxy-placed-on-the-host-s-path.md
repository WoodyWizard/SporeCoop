---
id: BUG-0503
title: Tribe: an animal proxy placed on the host's path stays 3.5-8 m off the host's NPC for 2-11 s (monitor rule proxies)
status: open
kind: desync
stage: Tribe
reported: 2026-11-03
source: BUG-0481 analysis, lab 20261103-012624
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0481, BUG-0361, BUG-0399
---

## Report
Split from BUG-0481 (play_tribe's third failure kind). The lab's monitor rule `proxies` failed play_tribe#3 of
out/lab/20261103-012624 (main checkout): `guest: 1 NPC proxies over 3 m off the host's NPC (Tribe) on 2 lines
from s 500`; the guest's log: `npc: proxy 5 (species 0680AA69) 5.2 m off the host's NPC for 2.1 s at (365.2
-51.6 347.4), host's speed 5.5 m/s (placed on the host's path)`, then 4.6 m for 2.4 s at 1.6 m/s; 20 s after
`an epic creature comes!`. The same run's campaign_tribe#3: proxy 54 (species 065CBE70) 3.5-7.4 m off for
2.1-11.3 s on both extra players, proxy 125 7.9 m for 2.3 s, all "placed on the host's path".
To reproduce: play_tribe (10 min) on the tribe fixture; seen in 1 of 3 play_tribe runs of 20261103-012624,
not in the 2 runs of fix/w40-play before its final check (20261103-150636, -152720).

## Root cause
Not analysed. Inferred: a proxy the mod places on the host's path every frame (npc.walk_tribe) should not be
3 m off for 2 s, so either the game did not keep the place (walk_place's place_lost: a herd mate or the epic
pushing it), or the host's NPC position used by the audit is not the one the placing aims at (the audit takes
the host's state at this proxy's radius, Tribe drives at its own height, BUG-0329). Next: `coop.npctrack` on
the guest around the report (how 1/3, place_lost, the drawn vs target rows).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
