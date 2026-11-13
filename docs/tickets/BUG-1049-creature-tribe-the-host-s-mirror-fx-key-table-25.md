---
id: BUG-1049
title: Creature/Tribe: the host's mirror_fx key table (256) is never cleared and drops a live key's stop when full: a looped effect can stay on a guest's mirror
status: open
kind: desync
stage: Creature,Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-20
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-mirror-fx-keys
related: BUG-0600, BUG-0955
---

## Report
Audit SPC-20 (the audit's mechanism is partly wrong, see Root cause). Not seen at runtime, found by code reading.
The host remembers which keyed effects it relayed a start for (so that the stop goes out once). The table never forgets entries of creatures that died or of a world that is gone, so over a long session it fills; once full, every new start evicts slot 0, whose later stop is never sent: the guest's mirror can keep a looped effect (a keyed sound or particle) until the mirror is destroyed.

## Root cause
Observed in code (main efd2e0ba): mods/coop/mirror_fx.cpp:109-129: `g_keys[256]` (host side), entries removed only by a stop of the same (space, id, key) (key_take, 112-120); key_put at 126-127 overwrites slot 0 when full ("its stop is not sent"). There is no on_session_end / on_world_change in mirror_fx (coop.cpp calls only init and on_message), so ids of a gone world (reused by npc / tribe ids in the next one) also find stale entries.
The audit's "looped effects in g_keys survive the world change" is wrong in detail: g_keys holds no effects, only the host's bookkeeping; the guest's effects belong to its mirrors, which a world change destroys.
Inferred: how often a keyed start has no stop from the game (a creature that dies or despawns while the key runs); without that the table does not fill. Confidence: medium.

## Fix idea
Clear g_keys on session end and world change; drop entries whose (space, id) is no longer mirrored (tribe::id_of / npc ids gone) once a second; when full, evict the oldest and send its stop. Switch: `coop.fix mirror.fx_keys off`.

## How to test
New check in `coop_tribe_fight_sounds.py` or `coop_creature_effects.py`: a test command fills the host's table with 256 starts (`coop.mirrorfx fill`), then a real keyed start / stop of a host member; assert the guest's mirror has no running effect of that key after the stop (the game's effect list of the mirror). Main: still running (FAIL). Plus `coop.mirrorfx` showing keys=0 after a world change.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
