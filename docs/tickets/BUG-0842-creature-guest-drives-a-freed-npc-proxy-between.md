---
id: BUG-0842
title: Creature guest drives a freed NPC proxy between the once-a-second noun-list checks
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9012
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-freed
commits: 570ee491
tests: proxy_identity, proxy_identity_tribe
signature: fork: coop D1A80 null vtable read 44 via npc guest_tick -> drive::step
docs: docs/re/creature.md#creature-freed-block-reuse, docs/06-testing.md#testing-freed-block-reuse
related: BUG-0426, BUG-0300, BUG-0170
---

## Report
A guest in the Creature stage can crash while it moves the host's animals (proxies) after a native death or respawn of
its own creature. The fork saw it after holding the guest's death caption for 125 s and confirming it: the guest crashed
before hatching, a virtual call on an object with a null vtable inside drive::step from npc::guest_tick.

## Root cause
Observed on our code and at runtime (fix/w48-freed, 2026-11-12). Since BUG-0931 (0.7.12) the guest checks its proxies against the creature list in every frame, but only in Creature, after the stale drop, and by address: the list says only that *a* creature lives there.
* Observed: a destroyed creature is freed 10-40 frames later and the next creature of its species gets the block (8-37 creations, docs/re/creature.md#creature-freed-block-reuse). That creature is listed and its mbIsDestroyed byte is clear.
* Observed: where the guest did not look in that time (the second after an avatar or world change, `coop.npc settle`; the fork's held death caption), the next frame drove the new creature as the host's animal (lab 20261112-004057 proxy #5 at 0.0 m from the host's NPC; 20261112-010612 proxies #6, #7), and the guest's own per-frame proxy audit (every 0.25 s, outside guest_tick) called the freed creature's vtables: caught faults in coop.dll game::cast, one guest crashed with a corrupted heap (MallocInternal, ecx = the freed proxy).
* Observed: in Tribe nothing but mbIsDestroyed was checked: 286 caught faults and a sync-check MISMATCH in 20261112-010017-2.
* Not ours: the fork's "avatar is null while dead" window; our guest's avatar keeps its address through death and respawn.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9012, commit fb08eac); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* npc.cpp guest_tick: the check_listed call is moved before the stale-removal loop and runs on every guest_tick pass
  in Creature (after the existing 1 s settle wait of a new world), not once a second. One noun-list scan per frame
  serves all proxies (the scan builds an unordered_set of listed creature pointers, noun 0x18EB45E, object_alive).
* check_listed: the early `if (a.listed.empty()) return;` is kept only for the old behaviour; with the fix an empty
  list also retires every cached proxy pointer (g_proxy_of erase, puppet_unregister, px.c = nullptr, drive state reset),
  so it is recreated when the world has creatures again.
* Switch `npc.proxy_listed_frame` (COOP_FIX, BUG-9012): off = the 0.7.x one-second schedule.
* Their test: duo_creature_rescue.py --hold-death 125 (guest killed, its death caption held 125 s, then confirmed);
  FAIL before (crash), PASS after (both hatch, migrate and walk). Our variant needs no fork feature: hold the guest's
  death screen, then confirm.
* Cost note for us: one noun-list scan per frame on the guest (the fork measured no problem; check at 15 fps with
  4 players).

## How to test
New lab test (e.g. coop_creature_proxy_freed.py, creature fixture, 2 games): kill the guest's avatar, hold its death
caption about 2 min while the host's animals move and die, confirm, wait for the hatch and 60 s of walking near
animals. Assert: no fault or crash report on the guest, and the guest's proxies match the host's NPC list
(coop.npc list on both). FAIL with `coop.fix npc.proxy_listed_frame off` (or on main), PASS with the fix. Repeat under
the chaos lane (objects freed and recreated) if it does not fail reliably.

## Fix
mods/coop/game.cpp: `game::listed` / `game::LiveRef` (one walk of the object list per frame, kept exact by the DestroyInstance hook and the serial counter; identity = pointer + serial). mods/coop/npc.cpp: each proxy records its creature's serial when it is made; in Creature and Tribe, before the stale drop and before any driving, a proxy whose creature is not that object any more is forgotten without a call on it and made again (an empty list retires them all); destroy_proxy, the audit, the sync entries, the away snapshot and the anim snap use the same check (`px_live`). Switch: `coop.fix npc.proxy_identity off` (address + mbIsDestroyed, the list check in Creature only). Test hooks: `coop.npc settle <s>`, `coop.npc reusehunt`.

## Verification
Tests `proxy_identity` and `proxy_identity_tribe` (tools/tests/coop_proxy_identity.py): the game destroys a proxy's creature while the guest's npc tick waits, a new creature of that species gets the freed block, then it must not be driven as the host's animal; no faults.
* FAIL (SPORE_FIX_OFF=npc.proxy_identity): out/lab/20261112-010612 (proxy_identity: proxies #6 and #7 driven onto the host's NPCs, 5 faults), out/lab/20261112-010017-2 (proxy_identity_tribe: 286 faults on the guest, sync-check MISMATCH). Before the audit used the check: out/lab/20261112-004057 (driven, 44 faults, the guest FROZE), out/lab/20261112-005401 (the guest crashed in MallocInternal).
* PASS: out/lab/20261112-010017, out/lab/20261112-013003 (both tests each).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: a candidate explanation of BUG-0170 (the 4-player Creature guest heap corruption of 2026-10-23, whose code had the same window without any list check). Scope note: check_listed runs only `game::in_creature_stage()` (npc.cpp:1789), so Tribe animal proxies are driven every frame with the +0x21 byte as the only check; the fix and its test should cover Tribe too (the guest's own tribe kills and frees proxies there, the comment at npc.cpp:1799).
- 2026-11-12: fixed on fix/w48-freed (npc.proxy_identity, the shared game::listed / LiveRef of BUG-0426); covers Tribe too.
