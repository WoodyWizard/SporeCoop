---
id: BUG-0842
title: Creature guest drives a freed NPC proxy between the once-a-second noun-list checks
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9012
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: coop D1A80 null vtable read 44 via npc guest_tick -> drive::step
docs:
related: BUG-0426, BUG-0300, BUG-0170
---

## Report
A guest in the Creature stage can crash while it moves the host's animals (proxies) after a native death or respawn of
its own creature. The fork saw it after holding the guest's death caption for 125 s and confirming it: the guest crashed
before hatching, a virtual call on an object with a null vtable inside drive::step from npc::guest_tick.

## Root cause
* Observed (code, rel/0.7.12): mods/coop/npc.cpp:1789 runs BUG-0300's check_listed (noun-list membership of every
  proxy) only once a second. Between checks guest_tick drives the cached `px.c` and only reads its mbIsDestroyed byte
  (`game::is_destroyed`, +0x21, npc.cpp:1806). Freed memory can have that byte clear again.
* Observed: the stale-removal loop (npc.cpp:1774-1788) runs before the check and calls destroy_proxy on a cached `px.c`
  that may already be freed.
* Observed: check_listed returns early on an empty list (npc.cpp:1702, "between worlds"), so cached pointers survive
  a world change unchecked.
* Inferred (from the fork's crash dump and its fix, not reproduced by us): confidence high that our code has the same
  window, medium that this exact trigger reaches it on our build. One piece of the umbrella BUG-0426.

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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: a candidate explanation of BUG-0170 (the 4-player Creature guest heap corruption of 2026-10-23, whose code had the same window without any list check). Scope note: check_listed runs only `game::in_creature_stage()` (npc.cpp:1789), so Tribe animal proxies are driven every frame with the +0x21 byte as the only check; the fix and its test should cover Tribe too (the guest's own tribe kills and frees proxies there, the comment at npc.cpp:1799).
