---
id: BUG-1078
title: Tribe: a guest that leaves after the host's game over and reload faults in its own creature_attack event (DramaManager_Get+0xBAA, data 0x120)
status: open
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: 
fixed_in:
branch:
commits:
tests: tribe_bounce_world (then any Tribe test that starts warm after it)
signature: 0xC0000005 at 0x00D521BA SporeCoop::DramaManager_Get+0xBAA (data 0x00000120), calls 00D4D536 00D4E84B 00B5B915 00D522E1
docs:
related: BUG-0582, BUG-0975
---

## Report
Found by the lab's warm resets (fix/w49-warm, lab 20261113-103507, lane 7; no player report). tribe_bounce_world
(the host's game over and its reload, the guest follows) passed. In the warm reset of the next test
(tribe_member_air) the guest's game left the session (`coop.leave`); 5 s later, alone, its Drama Manager started
the creature_attack event it had wanted to start four times during tribe_bounce_world (`drama: this game would start
its own creature_attack event: not on a guest`) and the game faulted once (caught by the game's handler, the game
went on and rejoined):

    drama: creature_attack event starts (host or alone)
    exception 0xC0000005 at 0x00D521BA SporeCoop::DramaManager_Get+0xBAA (data 0x00000120)
    called from: DramaManager_StartEvent+0x56 < DramaManager_StartKind+0x110B < ... < DramaManager_Update+0x71

(the names are the nearest symbols: DramaManager_Get+0xBAA is code past the getter, inside the event start). The lab
failed tribe_member_air for it (`guest faults: total faults: 1`). Logs: out/lab/20261113-103507/
tribe_member_air.guest.game.log (line ~580), tribe_bounce_world.guest.game.log.

A player meets it when a guest leaves a Tribe session (or the host leaves) after the host's game over and reload,
while its own game holds a creature_attack event back.

Reproduce: `tools/lab.py run tribe_bounce_world,tribe_member_air --in-order --lane N` (the fault needs the held
event: the guest's log must have the "would start its own creature_attack" line during tribe_bounce_world).

## Root cause
Not analysed. Inferred: the event the guest's Drama Manager held back refers to the world before the host's
reload (a tribe or target the reload freed); started alone after the leave, it reads a null + 0x120.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
