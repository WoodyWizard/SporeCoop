---
id: BUG-1049
title: Creature/Tribe: the host's mirror_fx key table (256) is never cleared and drops a live key's stop when full: a looped effect can stay on a guest's mirror
status: fixed
kind: desync
stage: Creature,Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-20
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-life
commits: f2f013ec, a49f379c, ade567a3, a7f75467, 398a5ab1
tests: mirror_fx_keys, lifecycle_reset
signature:
docs: docs/known-issues.md#issue-mirror-fx-keys, docs/01-architecture.md#arch-lifecycle-registry, docs/06-testing.md#testing-lifecycle-reset
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
mirror_fx.cpp: the host's key table is entry `mirror_fx.keys` of the lifecycle registry (session end and world
change), and a full table evicts its oldest entry (a sequence number per slot) and sends that entry's stop, so the
guest's mirror never keeps an effect whose stop was lost (status: `evicted=`, `lost=` for the old path). The rest of
the ticket's idea (dropping entries of creatures no longer mirrored once a second) is not needed: with the eviction
stop and the resets the table cannot keep a running effect. Switch `coop.fix mirror.fx_keys off` (0.7.13). Test
commands `coop.mirrorfx fill <n>`, `key start|relay|stop <member> <effect> [key] [pool]` (host), `has <member> <key>` (guest, with `stops=` received:
the game's effect in the mirror's pools, active or not).

## Verification
Test `mirror_fx_keys` (the sprint's dust 0xE55DC391 key 0x35052B7, pool 1, started on a host member, active on the
guest's mirror; the host's table filled with 256 starts; the host stops the effect: the guest's game must have it
stopped). FAIL (SPORE_FIX_OFF=mirror.fx_keys): out/lab/20261113-014717 (lost=1, the guest got no stop). PASS: out/lab/20261113-014719, out/lab/20261113-014827.
Earlier versions of the test: 0x8ADA6617 pool 1 never active on a mirror (SKIP, out/lab/20261113-010644); the
behaviour's pool-2 0x21C934A7 ends by itself within 1 s, so its FAIL twin passed (out/lab/20261113-012553, lost=1 but
the effect gone anyway; PASS runs 20261113-012450, 20261113-012647 do not count). docs/re/tribe.md#tribe-cycle-restart.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-life (package L1, the lifecycle registry of BUG-1055).
